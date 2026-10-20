# gw-server

A focused oat++ HTTP adapter over GW's native C++ renderer. No Python or gwplot
is required. One process owns one persistent dataset/view; requests are serialized
and unchanged PNG frames are cached. It uses CPU raster surfaces without creating
a GLFW window. This is a local development server, not a multi-user hosting service.

## Build

Use sibling checkouts (`gw/` and `gw-server/`). Install oat++ 1.3.x, CMake, and the
normal GW native build dependencies, including zlib. From GW:

```sh
make server GW_SERVER_DIR=../gw-server
```

For an oat++ installation outside standard paths:

```sh
make server GW_SERVER_DIR=../gw-server OATPP_ROOT=/path/to/oatpp/prefix
```

Alternatively supply a source checkout pinned to oat++ 1.3.1:

```sh
make server GW_SERVER_DIR=../gw-server OATPP_SOURCE_DIR=/path/to/oatpp-1.3.1
```

Only this optional target requires oat++; ordinary GW and the full WASM build do
not. `GW_SERVER_BUILD_DIR` can override the default `../gw-server/build` directory.
The GW Makefile supplies the native renderer archive and platform link flags.
Static dependencies must match the compiler/architecture and selected Skia headers.
The server link intentionally rejects unresolved symbols rather than using macOS
`dynamic_lookup`.

For MSYS2 UCRT64, oat++ is packaged as `mingw-w64-ucrt-x86_64-oatpp`; use the matching
UCRT64 compiler and GW dependencies. For conda builds, provide oat++ through the
build environment or pinned sources; the server needs no Python runtime. Linux
and Windows build/runtime validation remains CI work; development validation has
been on macOS arm64 with Skia m133 and oat++ 1.3.1.

## Run and test

From GW:

```sh
../gw-server/build/gw-server --reference test/test.fa --bam test/test.bam \
  --region chr1:9900-10300
```

In another terminal:

```sh
python3 ../gw-server/tests/requests_test.py
```

Run tests without another browser changing/resizing the shared view.
The test uses Python's standard library. It checks PNG dimensions, state updates,
caching, validation, stale frames/actions and concurrent mutations. Use `--url`
for a different port. Files are selected only at process startup; no endpoint
loads server paths or serves genomic files. The server binds to `127.0.0.1:8766`;
`--port` changes the port. Multiple browsers currently share the same view.

## Browser client

From GW, build the separate ImGui client using an activated Emscripten SDK:

```sh
make -C deps/wasm_remote
../gw-server/build/gw-server --reference test/test.fa --bam test/test.bam \
  --region chr1:9900-10300 --web-root "$PWD/deps/wasm_remote/out"
```

Open <http://127.0.0.1:8766/>. The client receives PNGs and small JSON state updates. It reuses GW's original
ImGui overlay through a data-free GW UI mirror, and receives no raw
alignment/reference files. The
original full WASM viewer remains in `deps/wasm`.

## Protocol v1

- `GET /health`: protocol version and health.
- `GET /api/state`: revision, region, theme, dimensions and alignment basenames.
- `POST /api/action`: JSON with the expected `revision` and `type`:
  - `navigate`: `value` is `chromosome:start-end`, matching GW region commands.
  - `command`: `value` is an allowed display command.
  - `resize`: `width` and `height` are physical image pixels, bounded to 32 megapixels.
    `pixel_ratio` (1..4, default 1) scales drawing metrics for HiDPI screens.
  - `settings`: `value` contains whitelisted numeric display settings. Invalid
    batches are rejected atomically.
- `GET /api/frame?revision=N`: binary PNG plus `X-GW-Revision`. A repeated request
  for the unchanged revision returns the cached PNG.

Successful actions return current state. Stale revisions return 409; invalid
actions return 400. Navigate requires a chromosome shared with the loaded
alignments and coordinates within both reference and alignment chromosome bounds. Command strings are passed to GW's native parser, which resolves chromosome-only
shorthand, single positions, full regions, feature names and command syntax.
Native errors are returned as Output text. File operations, shutdown, and
unsupported multi-region/UI lifecycle operations remain excluded.

The transport-independent `GwViewer::Session`, `State` and `Action` live in GW's
`protocol/viewer/native/viewer_interface`. Their public header exposes no oat++ or GW internals.
`protocol/viewer/native/viewer_settings.h` restricts render-setting synchronization to numeric display
parameters, excluding paths, file access and host configuration. No JSON parsing or
network requests enter native GW's drawing/read-packing loops.

The remote client calls the original `GwPlot::overlayImGui` implementation,
including normal region boxes, view menu, Tools and full-screen Settings. An
optional command callback routes UI commands to this server. Unsupported file,
multi-region and non-render settings are disabled instead of acting on local data.
PNG frames reserve space for the original overlay. Physical pixels and drawing
scale are separate; fonts/row gaps scale with DPR instead of shrinking on Retina.

Read/coverage popup queries, mouse pan/zoom, authentication and independent user
sessions are not implemented yet. The server remains local-only.

### Pointer input

`POST /api/action` accepts `type: "input"` with `value` containing
`press|release|move|wheel|cancel x y button mods dx dy`. Positions are physical
framebuffer pixels; the initial protocol supports the GLFW left button (0),
GLFW modifier bits, and bounded wheel deltas. Events carry the displayed frame's
revision and reuse GW's windowless mouse handlers. `popups` in the returned state
contains escaped records for the existing client-side ImGui popups. This sends
selected read/feature metadata and copyable SAM text to the browser, without
exposing dataset files. Read actions refer to bounded, server-owned selection IDs.

The browser coalesces movement while one request is in flight, preserves releases,
and cancels gestures on lost focus or stale-state reconciliation. Right-click mode
switching and Add mate are not supported yet. Test with:

```sh
python3 ../gw/test/test_remote_input.py http://127.0.0.1:8766
```

### Region panes and view keys

View state includes a `regions` array and `active_region`. The legacy `region`
field names the active pane, or is empty when all panes are closed. Commands
may include `region_index` to target a pane. `add REGION` and `rm INDEX` reuse
GW's native region lifecycle (up to 16 panes); `rm` cannot remove data files.
`key GLFW_KEY GLFW_MODS` dispatches supported navigation, read-scrolling,
link/find/repeat and row-limit hotkeys through GW's native handler.
Resize actions cancel active drags and refresh all pane layouts.

### Annotation tracks

Use repeatable `--track` arguments to load local annotations through GW's native
track loader. For the fixture demo, add `--track test/test.gff3 --track
test/server_demo.bed`. State includes `tracks` layout records containing physical
top/height and an escaped display name. `track-resize INDEX Y` invokes native
separator dragging once at the requested final physical Y coordinate. It retains
the active region and native minimum sizes/fraction persistence. Clients preview
the boundary locally and submit on release; feature clicks use existing popups.


## Versioned viewer interface

The WASM demo now connects through `POST /viewer/v1/message` and
`POST /viewer/v1/frame`, using the contract in GW's
`protocol/viewer/v1/README.md` and `schema.json`. The browser adapter is shipped
from that same directory; it has no oat++ dependency.

The startup dataset provisions a shared session called `default`. Up to eight
clients have independent renderer sizes, DPI, read selections, popups and active
gestures. Regions, filters, markers, settings and track layout are synchronized.
Heartbeats expose shared revisions without sending another PNG. The server
cancels timed-out gestures, retains disconnected clients for reconnection, and
bounds cached replies and frames. It binds localhost; authentication and dataset
access control belong to a hosting application.

The older `/api/state`, `/api/action` and `/api/frame` endpoints remain unchanged
and use a separate legacy view. Do not mix their revision numbers with viewer-v1.
The existing native GW/gwplot API is unchanged; the optional `GwViewer::Session`
wrapper supplies renderer replication/cancellation for this implementation.

Conformance checks from the GW repository:

```sh
python3 test/test_viewer_protocol_http.py http://127.0.0.1:8767
node test/test_viewer_protocol_http.mjs http://127.0.0.1:8767
```

Use the fixture server with `test.fa`, `test.bam`, `test.gff3` and
`server_demo.bed` for these tests.

### Local BAM drop demo

`/embed.html` is a full-window WASM viewer. Drop a coordinate-sorted BAM,
with its `.bai`/`.csi`. A BAM selected alone waits locally for its index;
no automatic indexing is performed. Files upload in 4 MiB chunks to isolated
`/uploads/{id}` sessions. The server reference
must match the BAM. The first mapped read supplies the initial locus.

This optional demo extension uses `/files/{bam,bai,csi}?offset=N`, `/open`,
`/message`, `/frame`, and `DELETE /uploads/{id}`; the viewer message/frame bodies
retain the existing v1 protocol. Four upload sessions and 8 GiB per file are
allowed. Owner-only temporary directories expire after 30 minutes without
requests and are deleted on normal shutdown. Deployments beyond localhost need
their own authentication/storage policy. Existing `/api/*` and `/viewer/v1/*`
interfaces are unchanged.
