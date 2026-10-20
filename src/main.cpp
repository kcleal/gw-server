#include "protocol/viewer/native/viewer_interface.h"
#include "oatpp/core/base/Environment.hpp"
#include "oatpp/core/macro/codegen.hpp"
#include "oatpp/core/macro/component.hpp"
#include "oatpp/network/tcp/server/ConnectionProvider.hpp"
#include "oatpp/network/Server.hpp"
#include "oatpp/web/server/HttpConnectionHandler.hpp"
#include "oatpp/web/server/api/ApiController.hpp"
#include "oatpp/parser/json/mapping/ObjectMapper.hpp"
#include <cstdlib>
#include <fstream>
#include <map>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <functional>
#include <thread>
#include <condition_variable>
#include "protocol.hpp"

#include OATPP_CODEGEN_BEGIN(DTO)
class ViewDto : public oatpp::DTO {
    DTO_INIT(ViewDto, DTO)
    DTO_FIELD(Int64, revision);
    DTO_FIELD(Int32, width);
    DTO_FIELD(Int32, height);
    DTO_FIELD(String, region);
    DTO_FIELD(String, theme);
    DTO_FIELD(String, settings);
    DTO_FIELD(String, popups);
    DTO_FIELD(String, output);
    DTO_FIELD(String, scrollbars);
    DTO_FIELD(String, tracks);
    DTO_FIELD(Float64, pixel_ratio);
    DTO_FIELD(Vector<String>, alignments);
    DTO_FIELD(Vector<String>, regions);
    DTO_FIELD(Int32, active_region);
};
class ActionDto : public oatpp::DTO {
    DTO_INIT(ActionDto, DTO)
    DTO_FIELD(Int64, revision);
    DTO_FIELD(String, type);
    DTO_FIELD(String, value);
    DTO_FIELD(Int32, width);
    DTO_FIELD(Int32, height);
    DTO_FIELD(Int32, region_index);
    DTO_FIELD(Float64, pixel_ratio);
};
#include OATPP_CODEGEN_END(DTO)

class Controller : public oatpp::web::server::api::ApiController {
    GwViewer::Session& session;
    std::mutex mutex;
    GwProtocol::Host protocol;
    GwViewer::Config uploadConfig;
    struct Upload {
        std::filesystem::path directory;
        std::unique_ptr<GwProtocol::Host> host;
        std::chrono::steady_clock::time_point touched=std::chrono::steady_clock::now();
    };
    std::map<std::string, Upload> uploads;
    void discardUpload(const std::string& id) {
        auto it=uploads.find(id); if(it==uploads.end()) return;
        const auto path=it->second.directory;
        uploads.erase(it);
        std::error_code error; std::filesystem::remove_all(path,error);
    }
    GwProtocol::Host& uploadHost(const std::string& id) {
        auto it=uploads.find(id);
        if(it==uploads.end() || !it->second.host) throw std::invalid_argument("Upload session not found");
        it->second.touched=std::chrono::steady_clock::now(); return *it->second.host;
    }
    std::condition_variable timer;
    bool stopping=false;
    std::thread sweeper;
    std::filesystem::path webRoot;
    oatpp::Object<ViewDto> dto(const GwViewer::State& s) {
        auto d = ViewDto::createShared();
        d->revision = s.revision; d->width = s.width; d->height = s.height;
        d->region = s.region; d->theme = s.theme;
        d->tracks = s.tracks; d->scrollbars = s.scrollbars; d->output = s.output; d->popups = s.popups; d->settings = s.settings; d->pixel_ratio = s.pixelRatio;
        d->regions = oatpp::Vector<oatpp::String>::createShared();
        for (const auto& region : s.regions) d->regions->push_back(region);
        d->active_region=s.activeRegion;
        d->alignments = oatpp::Vector<oatpp::String>::createShared();
        for (const auto& name : s.alignments) d->alignments->push_back(name);
        return d;
    }
    std::shared_ptr<OutgoingResponse> error(const Status& status, const char* text) {
        auto r = createResponse(status, text);
        r->putHeader("Content-Type", "text/plain; charset=utf-8");
        r->putHeader("Cache-Control", "no-store");
        return r;
    }
    template<class F> std::shared_ptr<OutgoingResponse> handle(F&& fn) {
        std::lock_guard<std::mutex> lock(mutex);
        try {
            auto r = fn();
            r->putHeader("Cache-Control", "no-store");
            r->putHeader("X-Content-Type-Options", "nosniff");
            return r;
        } catch (const GwViewer::StaleView& e) { return error(Status::CODE_409, e.what()); }
          catch (const std::invalid_argument& e) { return error(Status::CODE_400, e.what()); }
          catch (const std::runtime_error& e) { std::cerr << e.what() << '\n'; return error(Status::CODE_500, "Rendering failed"); }
          catch (const std::exception& e) { std::cerr << e.what() << '\n'; return error(Status::CODE_500, "Internal server error"); }
    }
public:
    Controller(GwViewer::Session& s, const GwViewer::Config& config, std::filesystem::path root)
        : ApiController(oatpp::parser::json::mapping::ObjectMapper::createShared()), session(s), protocol(config), uploadConfig(config), webRoot(std::move(root)) {
        sweeper=std::thread([this] {
            std::unique_lock<std::mutex> lock(mutex);
            while(!timer.wait_for(lock,std::chrono::seconds(5),[this]{return stopping;})) {
                try { protocol.sweep();
                    std::vector<std::string> expired;
                    for(auto& pair:uploads) {
                        if(pair.second.host) pair.second.host->sweep();
                        if(std::chrono::steady_clock::now()-pair.second.touched>std::chrono::minutes(30)) expired.push_back(pair.first);
                    }
                    for(const auto& id:expired) discardUpload(id);
                } catch(const std::exception& e) { std::cerr<<"Session cleanup: "<<e.what()<<'\n'; }
            }
        });
    }
    ~Controller() {
        { std::lock_guard<std::mutex> lock(mutex); stopping=true; }
        timer.notify_all(); if(sweeper.joinable()) sweeper.join();
        while(!uploads.empty()) discardUpload(uploads.begin()->first);
    }
#include OATPP_CODEGEN_BEGIN(ApiController)
    // Demo-only dataset upload extension. The versioned viewer wire format is unchanged.
    ENDPOINT("POST", "/uploads/{id}/files/{kind}", uploadChunk,
             PATH(String,id), PATH(String,kind), QUERY(Int64,offset), BODY_STRING(String,body)) {
        return handle([&]() {
            const auto key=GwProtocol::text(id), type=GwProtocol::text(kind);
            if(key.size()!=36 || key.find_first_not_of("0123456789abcdef-")!=std::string::npos)
                throw std::invalid_argument("Invalid upload ID");
            if(type!="bam" && type!="bai" && type!="csi") throw std::invalid_argument("Unsupported file type");
            if(!offset || *offset<0 || !body || body->size()>4*1024*1024) throw std::invalid_argument("Invalid upload chunk");
            auto it=uploads.find(key);
            if(it==uploads.end()) {
                if(*offset!=0 || uploads.size()>=4) throw std::invalid_argument("Upload capacity reached or invalid offset");
                const auto directory=std::filesystem::temp_directory_path()/("gw-upload-"+key);
                if(!std::filesystem::create_directory(directory)) throw std::invalid_argument("Upload ID already exists");
                std::filesystem::permissions(directory,std::filesystem::perms::owner_all);
                Upload entry; entry.directory=directory; it=uploads.emplace(key,std::move(entry)).first;
            }
            auto& upload=it->second;
            if(upload.host) throw std::invalid_argument("Upload already opened");
            const auto file=upload.directory/(type=="bam"?"reads.bam":"reads.bam."+type);
            const uint64_t size=std::filesystem::exists(file)?std::filesystem::file_size(file):0;
            if(uint64_t(*offset)!=size || size+body->size()>uint64_t(8)*1024*1024*1024)
                throw std::invalid_argument("Invalid upload offset or file exceeds 8 GiB");
            std::ofstream stream(file,std::ios::binary|std::ios::app);
            stream.write(body->data(),body->size()); stream.close();
            if(!stream) throw std::runtime_error("Cannot write upload");
            upload.touched=std::chrono::steady_clock::now();
            return createResponse(Status::CODE_200,"ok");
        });
    }
    ENDPOINT("POST", "/uploads/{id}/open", uploadOpen, PATH(String,id)) {
        return handle([&]() {
            auto it=uploads.find(GwProtocol::text(id));
            if(it==uploads.end()) throw std::invalid_argument("Upload not found");
            if(!it->second.host) {
                auto config=GwViewer::alignmentConfig(uploadConfig,(it->second.directory/"reads.bam").string());
                it->second.host=std::make_unique<GwProtocol::Host>(config);
            }
            return createResponse(Status::CODE_200,"ok");
        });
    }
    ENDPOINT("DELETE", "/uploads/{id}", uploadDelete, PATH(String,id)) {
        return handle([&]() { discardUpload(GwProtocol::text(id)); return createResponse(Status::CODE_200,"ok"); });
    }
    ENDPOINT("POST", "/uploads/{id}/message", uploadMessage, PATH(String,id), BODY_STRING(String,body)) {
        return handle([&]() {
            auto result=uploadHost(GwProtocol::text(id)).message(GwProtocol::text(body));
            auto response=createResponse(Status::CODE_200,result.c_str());
            response->putHeader("Content-Type","application/json"); return response;
        });
    }
    ENDPOINT("POST", "/uploads/{id}/frame", uploadFrame, PATH(String,id), BODY_STRING(String,body)) {
        return handle([&]() {
            auto bytes=uploadHost(GwProtocol::text(id)).frame(GwProtocol::text(body));
            auto response=createResponse(Status::CODE_200,oatpp::String(reinterpret_cast<const char*>(bytes.data()),bytes.size()));
            response->putHeader("Content-Type","image/png"); return response;
        });
    }
    ENDPOINT("POST", "/viewer/v1/message", protocolMessage, BODY_STRING(String, body)) {
        std::lock_guard<std::mutex> lock(mutex);
        auto result=protocol.message(GwProtocol::text(body));
        auto response=createResponse(Status::CODE_200,result.c_str());
        response->putHeader("Content-Type","application/json"); response->putHeader("Cache-Control","no-store");
        return response;
    }
    ENDPOINT("POST", "/viewer/v1/frame", protocolFrame, BODY_STRING(String, body)) {
        std::lock_guard<std::mutex> lock(mutex);
        try {
            auto bytes=protocol.frame(GwProtocol::text(body));
            auto response=createResponse(Status::CODE_200,oatpp::String(reinterpret_cast<const char*>(bytes.data()),bytes.size()));
            response->putHeader("Content-Type","image/png"); response->putHeader("Cache-Control","no-store"); return response;
        } catch(const GwProtocol::Error& e) {
            auto value=VError::createShared(); value->code=e.code; value->message=e.what();
            auto wrapper=oatpp::Fields<oatpp::Object<VError>>::createShared(); wrapper->push_back({"error",value});
            return createDtoResponse(Status::CODE_409,wrapper);
        } catch(const std::exception&) { return error(Status::CODE_400,"Invalid frame request"); }
    }
    ENDPOINT("GET", "/health", health) {
        auto r = createResponse(Status::CODE_200, "{\"status\":\"ok\",\"protocol\":1}");
        r->putHeader("Content-Type", "application/json");
        return r;
    }
    ENDPOINT("GET", "/api/state", state) {
        return handle([&] { return createDtoResponse(Status::CODE_200, dto(session.state())); });
    }
    ENDPOINT("POST", "/api/action", action, BODY_DTO(Object<ActionDto>, body)) {
        return handle([&]() -> std::shared_ptr<OutgoingResponse> {
            if (!body || !body->revision || !body->type || body->revision < 1)
                throw std::invalid_argument("revision and type are required");
            GwViewer::Action a;
            const std::string type = *body->type;
            if (type == "navigate") a.type = GwViewer::Action::Type::Navigate;
            else if (type == "command") a.type = GwViewer::Action::Type::Command;
            else if (type == "resize") a.type = GwViewer::Action::Type::Resize;
            else if (type == "input") a.type = GwViewer::Action::Type::Input;
            else if (type == "settings") a.type = GwViewer::Action::Type::Settings;
            else throw std::invalid_argument("Unknown action type");
            if (body->value) a.value = *body->value;
            a.regionIndex = body->region_index ? body->region_index.getValue(-1) : -1;
            a.pixelRatio = body->pixel_ratio ? body->pixel_ratio.getValue(1) : 1;
            a.width = body->width ? body->width.getValue(0) : 0;
            a.height = body->height ? body->height.getValue(0) : 0;
            return createDtoResponse(Status::CODE_200, dto(session.apply(body->revision, a)));
        });
    }
    ENDPOINT("GET", "/api/frame", frame, QUERY(Int64, revision)) {
        return handle([&]() -> std::shared_ptr<OutgoingResponse> {
            if (!revision || revision < 1) throw std::invalid_argument("revision is required");
            const auto& png = session.frame(revision);
            auto r = createResponse(Status::CODE_200, oatpp::String(reinterpret_cast<const char*>(png.data()), png.size()));
            r->putHeader("Content-Type", "image/png");
            r->putHeader("X-GW-Revision", std::to_string(revision.getValue(0)).c_str());
            return r;
        });
    }
    ENDPOINT("GET", "/", index) { return asset("index.html"); }
    ENDPOINT("GET", "/{name}", staticFile, PATH(String, name)) { return asset(*name); }
#include OATPP_CODEGEN_END(ApiController)
    std::shared_ptr<OutgoingResponse> asset(const std::string& name) {
        // Only the compiled client assets are exposed; dataset paths never become URLs.
        if (webRoot.empty() || (name != "index.html" && name != "viewer.js" && name != "viewer.wasm" && name != "client.mjs" && name != "embed.mjs" && name != "embed.html"))
            return error(Status::CODE_404, "Not found");
        auto data = oatpp::String::loadFromFile((webRoot / name).string().c_str());
        if (!data) return error(Status::CODE_404, "Build the WASM client first");
        auto r = createResponse(Status::CODE_200, data);
        r->putHeader("Content-Type", (name == "index.html" || name == "embed.html") ? "text/html; charset=utf-8" : (name == "viewer.js" || name == "client.mjs" || name == "embed.mjs") ? "application/javascript" : "application/wasm");
        r->putHeader("Cache-Control", "no-store");
        return r;
    }
};

int main(int argc, char** argv) {
    try {
        GwViewer::Config config;
        int port = 8766;
        std::string host = "127.0.0.1", webRoot;
        for (int i = 1; i < argc; ++i) {
            const std::string key = argv[i];
            if (key == "--help") {
                std::cout << "gw-server --reference ref.fa --bam reads.bam --region chr1:9900-10300 [--track annotations.gff3] [--port 8766] [--web-root GW/deps/wasm_remote/out]\n";
                return 0;
            }
            if (++i >= argc) throw std::invalid_argument("Missing argument value");
            const std::string value = argv[i];
            if (key == "--reference") config.reference = value;
            else if (key == "--bam") config.alignments.push_back(value);
            else if (key == "--track") config.tracks.push_back(value);
            else if (key == "--region") config.region = value;
            else if (key == "--port") port = std::stoi(value);
            else if (key == "--web-root") webRoot = value;
            else throw std::invalid_argument("Unknown option: " + key);
        }
        if (config.reference.empty() || config.region.empty() || port < 1 || port > 65535)
            throw std::invalid_argument("Provide --reference and --region, with a valid port");
        GwViewer::Session session(config);
        oatpp::base::Environment::init();
        {
            auto router = oatpp::web::server::HttpRouter::createShared();
            auto controller = std::make_shared<Controller>(session, config, webRoot);
            router->addController(controller);
            auto handler = oatpp::web::server::HttpConnectionHandler::createShared(router);
            auto provider = oatpp::network::tcp::server::ConnectionProvider::createShared({host.c_str(), static_cast<v_uint16>(port), oatpp::network::Address::IP_4});
            std::cout << "GW server: http://" << host << ':' << port << " (one dataset; shared default session, per-client viewports)" << std::endl;
            oatpp::network::Server server(provider, handler);
            server.run();
        }
        oatpp::base::Environment::destroy();
        return 0;
    } catch (const std::invalid_argument& e) { std::cerr << "Error: " << e.what() << '\n'; }
      catch (const std::runtime_error& e) { std::cerr << "Error: " << e.what() << '\n'; }
      catch (const std::exception& e) { std::cerr << "Error: " << e.what() << '\n'; }
    return 1;
}
