#pragma once
#include "protocol/viewer/native/viewer_interface.h"
#include "protocol/viewer/v1/types.hpp"
#include <chrono>
#include <deque>
#include <map>
#include <random>
#include <sstream>
#include <iomanip>
#include <cmath>

// oat++ JSON binding for GW's optional versioned viewer protocol.
#include OATPP_CODEGEN_BEGIN(DTO)
class VViewport : public oatpp::DTO {
    DTO_INIT(VViewport, DTO)
    DTO_FIELD(Fields<Any>, extensions);
    DTO_FIELD(Int32, width); DTO_FIELD(Int32, height); DTO_FIELD(Float64, pixel_ratio);
};
class VAction : public oatpp::DTO {
    DTO_INIT(VAction, DTO)
    DTO_FIELD(Fields<Any>, extensions);
    DTO_FIELD(String, type); DTO_FIELD(String, region); DTO_FIELD(String, text);
    DTO_FIELD(Int32, region_index); DTO_FIELD(Fields<Any>, values);
    DTO_FIELD(String, phase); DTO_FIELD(String, button); DTO_FIELD(Vector<String>, modifiers);
    DTO_FIELD(Float64, x); DTO_FIELD(Float64, y); DTO_FIELD(Float64, dx); DTO_FIELD(Float64, dy);
    DTO_FIELD(String, key); DTO_FIELD(String, collection_id); DTO_FIELD(Int32, offset);
    DTO_FIELD(String, track_id);
};
class VRequest : public oatpp::DTO {
    DTO_INIT(VRequest, DTO)
    DTO_FIELD(Fields<Any>, extensions);
    DTO_FIELD(String, protocol); DTO_FIELD(Int32, version); DTO_FIELD(String, type);
    DTO_FIELD(String, request_id); DTO_FIELD(String, session_id); DTO_FIELD(String, client_id);
    DTO_FIELD(String, connection_id); DTO_FIELD(String, resume_token); DTO_FIELD(String, sequence);
    DTO_FIELD(String, expected_revision); DTO_FIELD(String, frame_id);
    DTO_FIELD(Object<VViewport>, viewport); DTO_FIELD(Object<VAction>, action);
};
class VShared : public oatpp::DTO {
    DTO_INIT(VShared, DTO)
    DTO_FIELD(Vector<String>, regions); DTO_FIELD(Vector<String>, alignments);
    DTO_FIELD(String, theme); DTO_FIELD(Fields<Float64>, settings);
};
class VPrivate : public oatpp::DTO {
    DTO_INIT(VPrivate, DTO)
    DTO_FIELD(String, event_id); DTO_FIELD(Int32, active_region); DTO_FIELD(String, popups); DTO_FIELD(String, output);
};
class VLayout : public oatpp::DTO {
    DTO_INIT(VLayout, DTO)
    DTO_FIELD(String, encoding); DTO_FIELD(String, scrollbars); DTO_FIELD(String, tracks);
};
class VFrame : public oatpp::DTO {
    DTO_INIT(VFrame, DTO)
    DTO_FIELD(String, frame_id); DTO_FIELD(String, revision); DTO_FIELD(Object<VViewport>, viewport);
    DTO_FIELD(Int32, width); DTO_FIELD(Int32, height); DTO_FIELD(String, mime_type);
    DTO_FIELD(Object<VLayout>, layout);
};
class VView : public oatpp::DTO {
    DTO_INIT(VView, DTO)
    DTO_FIELD(String, revision); DTO_FIELD(Object<VShared>, shared); DTO_FIELD(Object<VPrivate>, client);
    DTO_FIELD(Object<VFrame>, frame);
};
class VError : public oatpp::DTO {
    DTO_INIT(VError, DTO)
    DTO_FIELD(String, code); DTO_FIELD(String, message);
};
class VReply : public oatpp::DTO {
    DTO_INIT(VReply, DTO)
    DTO_FIELD(String, protocol); DTO_FIELD(Int32, version); DTO_FIELD(String, type); DTO_FIELD(String, request_id);
    DTO_FIELD(String, session_id); DTO_FIELD(String, client_id); DTO_FIELD(String, connection_id); DTO_FIELD(String, resume_token);
    DTO_FIELD(String, last_sequence); DTO_FIELD(Float64, heartbeat_seconds); DTO_FIELD(Vector<String>, capabilities);
    DTO_FIELD(Object<VView>, view); DTO_FIELD(String, revision); DTO_FIELD(Object<VError>, error);
};
#include OATPP_CODEGEN_END(DTO)

namespace GwProtocol {
using Clock = std::chrono::steady_clock;
struct Error : std::runtime_error { std::string code; Error(std::string c,std::string m):std::runtime_error(m),code(std::move(c)){} };
inline void require(bool ok,const char* message) { if (!ok) throw Error("invalid_message",message); }
inline std::string text(const oatpp::String& s) { return s ? std::string(s->data(),s->size()) : ""; }
inline std::string token() {
    std::random_device entropy;
    std::ostringstream out;
    for (int i=0;i<8;++i) out<<std::hex<<std::setw(8)<<std::setfill('0')<<entropy();
    return out.str();
}
inline bool id(const oatpp::String& value) {
    auto s=text(value); return !s.empty() && s.size()<=128 && std::all_of(s.begin(),s.end(),[](unsigned char c){return c>=33&&c<=126;});
}
inline std::uint64_t counter(const oatpp::String& value) {
    try { return GwViewerProtocol::V1::counter(text(value)); }
    catch (...) { throw Error("invalid_message","Invalid uint64 counter string"); }
}
inline GwViewerProtocol::V1::Viewport viewport(const oatpp::Object<VViewport>& v) {
    require(v&&v->width&&v->height&&v->pixel_ratio,"Viewport requires width, height and pixel_ratio");
    GwViewerProtocol::V1::Viewport result{*v->width,*v->height,*v->pixel_ratio};
    try { result.framebuffer(); } catch (...) { throw Error("invalid_viewport","Invalid framebuffer dimensions"); }
    return result;
}
inline auto viewportDto(GwViewerProtocol::V1::Viewport v) {
    auto d=VViewport::createShared(); d->width=v.width; d->height=v.height; d->pixel_ratio=v.pixel_ratio; return d;
}
struct SavedFrame { std::string id; std::uint64_t revision; std::vector<std::uint8_t> png; };
struct CachedReply { std::string body,reply; };
struct Client {
    std::unique_ptr<GwViewer::Session> renderer;
    std::string resume,connection;
    GwViewerProtocol::V1::Viewport viewport;
    std::uint64_t seenRevision=0,sequence=0;
    Clock::time_point seen=Clock::now(), disconnected{},gestureSeen{};
    bool offline=false,gesture=false;
    std::map<std::uint64_t,CachedReply> replies;
    std::deque<SavedFrame> frames;
};

class Host {
    GwViewer::Config config;
    GwViewer::Session canonical;
    std::map<std::string,Client> clients;
    std::uint64_t revision=1;
    std::shared_ptr<oatpp::parser::json::mapping::ObjectMapper> mapper;
    void cancel(Client& c) {
        c.renderer->cancelInput(); c.gesture=false;
    }
    void sync(Client& c) {
        if(c.seenRevision!=revision) { cancel(c); c.renderer->synchronizeSharedFrom(canonical); c.seenRevision=revision; }
    }
    oatpp::Object<VReply> reply(const std::string& request,const char* kind) {
        auto r=VReply::createShared(); r->protocol="gw-viewer"; r->version=1; r->request_id=request; r->type=kind; return r;
    }
    oatpp::Object<VView> view(Client& c) {
        sync(c);
        auto state=c.renderer->state();
        auto v=VView::createShared(); v->revision=std::to_string(revision);
        v->shared=VShared::createShared(); v->shared->theme=state.theme;
        v->shared->regions=oatpp::Vector<oatpp::String>::createShared();
        for(auto& x:state.regions) v->shared->regions->push_back(x);
        v->shared->alignments=oatpp::Vector<oatpp::String>::createShared();
        for(auto& x:state.alignments) v->shared->alignments->push_back(x);
        v->shared->settings=oatpp::Fields<oatpp::Float64>::createShared();
        std::istringstream input(state.settings); std::string key; double value;
        while(input>>key>>value) v->shared->settings->push_back({key,value});
        v->client=VPrivate::createShared(); v->client->event_id=std::to_string(state.revision); v->client->active_region=state.activeRegion;
        v->client->popups=state.popups; v->client->output=state.output;
        auto f=VFrame::createShared(); f->frame_id=token(); f->revision=v->revision;
        f->viewport=viewportDto(c.viewport); f->width=state.width; f->height=state.height; f->mime_type="image/png";
        f->layout=VLayout::createShared(); f->layout->encoding="gw-ui-text-1";
        f->layout->scrollbars=state.scrollbars; f->layout->tracks=state.tracks;
        v->frame=f;
        c.frames.push_back({text(f->frame_id),revision,c.renderer->frame(state.revision)});
        while(c.frames.size()>2) c.frames.pop_front();
        return v;
    }
    std::string serialize(const oatpp::Object<VReply>& r) { return text(mapper->writeToString(r)); }
    void resize(Client& c,const oatpp::Object<VViewport>& v) {
        cancel(c); c.viewport=viewport(v); auto size=c.viewport.framebuffer();
        GwViewer::Action a{}; a.type=GwViewer::Action::Type::Resize; a.width=size.width; a.height=size.height; a.pixelRatio=c.viewport.pixel_ratio;
        c.renderer->apply(c.renderer->state().revision,a); c.frames.clear();
    }
    GwViewer::Action action(const oatpp::Object<VAction>& v,Client& c) {
        require(v&&v->type,"Missing action");
        GwViewer::Action a{}; a.regionIndex=v->region_index ? *v->region_index : -1;
        require(!v->region_index||(*v->region_index>=0&&*v->region_index<=65535),"Invalid region index");
        auto kind=text(v->type); a.type=GwViewer::Action::Type::Command;
        if(kind=="command"||kind=="navigate") {
            a.type=kind=="command"?GwViewer::Action::Type::Command:GwViewer::Action::Type::Navigate;
            a.value=text(kind=="command"?v->text:v->region);
            require(!a.value.empty()&&a.value.size()<=4096&&a.value.find_first_of("\r\n\0",0,3)==std::string::npos,"Invalid command/region");
        } else if(kind=="settings") {
            require(v->values&&v->values->size()&&v->values->size()<=128,"Invalid settings");
            a.type=GwViewer::Action::Type::Settings;
            std::ostringstream values;
            for(auto& item:*v->values) {
                double number;
                auto type=item.second.getStoredType();
                if(type==oatpp::Float64::Class::getType()) number=*item.second.retrieve<oatpp::Float64>();
                else if(type==oatpp::Int64::Class::getType()) number=double(*item.second.retrieve<oatpp::Int64>());
                else if(type==oatpp::UInt64::Class::getType()) number=double(*item.second.retrieve<oatpp::UInt64>());
                else if(type==oatpp::Boolean::Class::getType()) number=*item.second.retrieve<oatpp::Boolean>();
                else throw Error("unsupported_action","GW display settings must be numbers or booleans");
                require(std::isfinite(number),"Non-finite setting"); values<<text(item.first)<<' '<<number<<'\n';
            }
            a.value=values.str();
        } else if(kind=="pointer"||kind=="key") {
            int mods=0;
            require(bool(v->modifiers),"Missing modifiers");
            for(auto& mod:*v->modifiers) {
                int flag=text(mod)=="shift"?1:text(mod)=="control"?2:text(mod)=="alt"?4:text(mod)=="meta"?8:0;
                require(flag&&!(mods&flag),"Invalid modifiers"); mods|=flag;
            }
            std::ostringstream value;
            if(kind=="key") {
                static const std::map<std::string,int> keys={{"ArrowLeft",263},{"ArrowRight",262},{"ArrowUp",265},{"ArrowDown",264},{"PageUp",266},{"PageDown",267},{"BracketLeft",91},{"BracketRight",93},{"KeyL",76},{"KeyF",70},{"KeyR",82},{"Minus",45},{"Equal",61}};
                auto k=keys.find(text(v->key)); require(k!=keys.end(),"Unsupported key"); value<<"key "<<k->second<<' '<<mods;
            } else {
                const auto phase=text(v->phase),button=text(v->button);
                require(phase=="press"||phase=="move"||phase=="release"||phase=="wheel"||phase=="cancel","Invalid phase");
                require(button=="left"||button=="none","Only left pointer gestures supported");
                require(v->x&&v->y&&std::isfinite(*v->x)&&std::isfinite(*v->y)&&std::abs(*v->x)<=1e7&&std::abs(*v->y)<=1e7,"Invalid pointer coordinates");
                if(phase=="wheel") require(v->dx&&v->dy,"Wheel requires dx/dy");
                double dx=v->dx?*v->dx:0,dy=v->dy?*v->dy:0;
                require(std::isfinite(dx)&&std::isfinite(dy)&&std::abs(dx)<=1000&&std::abs(dy)<=1000,"Invalid wheel steps");
                auto size=c.viewport.framebuffer();
                const auto x=std::clamp(*v->x*c.viewport.pixel_ratio,0.,double(size.width));
                const auto y=std::clamp(*v->y*c.viewport.pixel_ratio,0.,double(size.height));
                a.type=GwViewer::Action::Type::Input; value<<phase<<' '<<x<<' '<<y<<" 0 "<<mods<<' '<<dx<<' '<<dy;
            }
            a.value=value.str();
        } else if(kind=="vscroll") {
            require(id(v->collection_id)&&v->offset&&*v->offset>=0,"Invalid scroll offset");
            const auto index=text(v->collection_id); require(index.find_first_not_of("0123456789")==std::string::npos,"Invalid collection ID");
            a.value="vscroll "+index+" "+std::to_string(*v->offset);
        } else if(kind=="track_resize") {
            require(id(v->track_id)&&v->y&&std::isfinite(*v->y),"Invalid track resize");
            const auto index=text(v->track_id); require(index.find_first_not_of("0123456789")==std::string::npos,"Invalid track ID");
            a.value="track-resize "+index+" "+std::to_string(*v->y*c.viewport.pixel_ratio);
        } else throw Error("unsupported_action","Unknown action type");
        return a;
    }
public:
    explicit Host(const GwViewer::Config& cfg):config(cfg),canonical(cfg) {
        auto serializer=oatpp::parser::json::mapping::Serializer::Config::createShared(); serializer->includeNullFields=false;
        auto deserializer=oatpp::parser::json::mapping::Deserializer::Config::createShared(); deserializer->allowUnknownFields=false;
        mapper=oatpp::parser::json::mapping::ObjectMapper::createShared(serializer,deserializer);
    }
    // Caller serializes entry and runs sweep periodically even when no requests arrive.
    void sweep() {
        auto now=Clock::now();
        for(auto i=clients.begin();i!=clients.end();) {
            auto& c=i->second;
            if(!c.offline&&now-c.seen>std::chrono::seconds(30)) { cancel(c); c.offline=true; c.disconnected=now; c.frames.clear(); }
            if(c.gesture&&now-c.gestureSeen>std::chrono::seconds(15)) cancel(c);
            if(c.offline&&now-c.disconnected>std::chrono::seconds(120)) i=clients.erase(i); else ++i;
        }
    }
    std::string message(const std::string& body) {
        oatpp::Object<VRequest> r; std::string request="invalid";
        try {
            require(body.size()<=65536,"Message too large");
            try { r=mapper->readFromString<oatpp::Object<VRequest>>(body); } catch(...) { throw Error("invalid_message","Invalid JSON message"); }
            require(bool(r),"Missing message");
            if(id(r->request_id)) request=text(r->request_id);
            if(text(r->protocol)!="gw-viewer"||!r->version||*r->version!=1) throw Error("unsupported_version","Expected gw-viewer version 1");
            require(id(r->request_id)&&id(r->session_id),"Missing request/session ID");
            if(text(r->session_id)!="default") throw Error("session_not_found","Unknown session");
            sweep();
            const auto kind=text(r->type);
            if(kind=="join") {
                viewport(r->viewport);
                std::string cid=text(r->client_id);
                if(cid.empty()) {
                    require(!r->resume_token,"Resume requires client_id");
                    if(clients.size()>=8) throw Error("capacity","At most eight clients per demo session");
                    cid=token(); Client c; c.renderer=std::make_unique<GwViewer::Session>(config); c.resume=token();
                    clients.emplace(cid,std::move(c));
                } else if(!clients.count(cid)||clients.at(cid).resume!=text(r->resume_token)) throw Error("client_expired","Resume credentials expired");
                auto& c=clients.at(cid); c.connection=token(); c.offline=false; c.seen=Clock::now(); c.replies.clear();
                resize(c,r->viewport); sync(c);
                auto out=reply(request,"welcome"); out->session_id="default"; out->client_id=cid; out->connection_id=c.connection;
                out->resume_token=c.resume; out->last_sequence=std::to_string(c.sequence); out->heartbeat_seconds=10;
                out->capabilities=oatpp::Vector<oatpp::String>::createShared();
                for(auto k:{"command","navigate","settings","pointer","key","vscroll","track_resize","gw-ui-text-1"}) out->capabilities->push_back(k);
                out->view=view(c); return serialize(out);
            }
            require(id(r->client_id)&&id(r->connection_id),"Missing client identity");
            auto it=clients.find(text(r->client_id));
            if(it==clients.end()||it->second.offline||it->second.connection!=text(r->connection_id)) throw Error("client_expired","Join or reconnect first");
            auto& c=it->second; c.seen=Clock::now(); const auto seq=counter(r->sequence);
            auto cached=c.replies.find(seq);
            if(cached!=c.replies.end()) {
                if(cached->second.body!=body) throw Error("sequence_conflict","Sequence already used");
                return cached->second.reply;
            }
            if(seq<=c.sequence) throw Error("duplicate_expired","Request processed; request a snapshot");
            c.sequence=seq;
            auto finish=[&](std::string value) { c.replies[seq]={body,value}; while(c.replies.size()>64)c.replies.erase(c.replies.begin()); return value; };
            try {
                auto out=reply(request,"state");
                if(kind=="heartbeat"||kind=="leave") {
                    out->type="ack"; out->revision=std::to_string(revision);
                    if(kind=="leave") { cancel(c); c.offline=true; c.disconnected=Clock::now()-std::chrono::seconds(121); c.frames.clear(); }
                } else {
                    if(kind=="viewport") resize(c,r->viewport);
                    else if(kind=="action") {
                        require(r->action&&r->action->type,"Missing action");
                        const auto type=text(r->action->type),phase=text(r->action->phase);
                        counter(r->expected_revision);
                        const bool cancelling=type=="pointer"&&phase=="cancel";
                        auto native=action(r->action,c);
                        if(cancelling) cancel(c);
                        else {
                            if(counter(r->expected_revision)!=revision) { cancel(c); throw Error("stale_revision","Shared view changed; request a snapshot"); }
                            sync(c);
                            if(type=="pointer"||type=="key"||type=="vscroll"||type=="track_resize") {
                                auto fid=text(r->frame_id);
                                auto f=std::find_if(c.frames.begin(),c.frames.end(),[&](const auto& x){return x.id==fid&&x.revision==revision;});
                                if(f==c.frames.end()) {cancel(c); throw Error("stale_frame","Frame expired; request a snapshot");}
                            }
                            if(type=="pointer"&&phase=="release"&&!c.gesture) throw Error("gesture_expired","Gesture cancelled; start again");
                            if(type=="pointer"&&phase=="press"&&c.gesture) cancel(c);
                            auto before=c.renderer->state();
                            auto after=c.renderer->apply(before.revision,native);
                            const bool shared=before.regions!=after.regions||before.settings!=after.settings||before.theme!=after.theme||before.tracks!=after.tracks||
                                (type=="command"&&native.value.rfind("vscroll ",0)!=0&&native.value.rfind("key ",0)!=0)||type=="track_resize";
                            if(shared) {
                                if(revision==UINT64_MAX) throw Error("capacity","Revision exhausted");
                                canonical.synchronizeSharedFrom(*c.renderer,false);
                                config.alignments.erase(std::remove_if(config.alignments.begin(),config.alignments.end(),[&](const auto& path){
                                    return std::find(after.alignments.begin(),after.alignments.end(),std::filesystem::path(path).filename().string())==after.alignments.end();
                                }),config.alignments.end());
                                ++revision; c.seenRevision=revision;
                                for(auto& other:clients) if(&other.second!=&c&&other.second.gesture) cancel(other.second);
                            }
                            if(type=="pointer") { if(phase=="press") c.gesture=true; if(phase=="release") c.gesture=false; c.gestureSeen=Clock::now(); }
                        }
                    } else require(kind=="snapshot","Unknown message type");
                    out->view=view(c);
                }
                return finish(serialize(out));
            } catch(const Error& e) { auto out=reply(request,"error"); out->error=VError::createShared(); out->error->code=e.code; out->error->message=e.what(); return finish(serialize(out)); }
            catch(const std::invalid_argument& e) { auto out=reply(request,"error"); out->error=VError::createShared(); out->error->code="invalid_action"; out->error->message=e.what(); return finish(serialize(out)); }
            catch(...) { cancel(c); auto out=reply(request,"error"); out->error=VError::createShared(); out->error->code="backend_error"; out->error->message="Backend failed; request a snapshot"; return finish(serialize(out)); }
        } catch(const Error& e) {auto out=reply(request,"error");out->error=VError::createShared();out->error->code=e.code;out->error->message=e.what();return serialize(out);}
        catch(const std::exception&) {auto out=reply(request,"error");out->error=VError::createShared();out->error->code="backend_error";out->error->message="Backend failed; request a snapshot";return serialize(out);}
    }
    std::vector<std::uint8_t> frame(const std::string& body) {
        require(body.size()<=65536,"Frame request too large");
        auto values=mapper->readFromString<oatpp::Fields<oatpp::String>>(body);
        require(values&&values->size()==4,"Invalid frame identity");
        std::map<std::string,std::string> v; for(auto& x:*values) v[text(x.first)]=text(x.second);
        if(v["session_id"]!="default") throw Error("session_not_found","Unknown session");
        sweep(); auto i=clients.find(v["client_id"]);
        if(i==clients.end()||i->second.offline||i->second.connection!=v["connection_id"]) throw Error("client_expired","Join first");
        i->second.seen=Clock::now();
        for(auto& f:i->second.frames) if(f.id==v["frame_id"]) return f.png;
        throw Error("stale_frame","Frame expired; request a snapshot");
    }
};
}
