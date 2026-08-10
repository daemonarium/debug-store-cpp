# debug-store (WASM)

C++ WASM module that records debug sessions (snapshots, I/O, logs, spans, metrics) and persists them through **ts-fs** over sockets. Host logic stays in `ts-fs`; this module is portable across runners. A native per-runner implementation can replace the WASM binary later without changing the RPC contract.

## Architecture

```
Recorder (LocalProcess / shield)
    │  RPC begin on USER=0
    │  bind → session socket ≥2
    │  messages + debug_session RPC
    ▼
debug-store.wasm
    │  FS client on socket 1
    ▼
ts-fs LocalProcess
    ▼
host filesystem (rootDir)
```

| Socket | Role |
|--------|------|
| 0 USER | inbound `debug_store.begin` |
| 1 FS | outbound RPC to ts-fs (host must bind before first `begin`) |
| ≥2 | per-session: event messages + `debug_session` RPC |

USER, session, and FS client RPC use **flat** wire (`flat_socket_rpc_*`). Recorder dumps land here as opaque snapshot / `io.bin` / registry blobs — see [recorder/DESIGN.md](../recorder/DESIGN.md).

## API

**`debug_store.begin({ name, process_id, labels })`** → `{ session_id, store_process, store_socket }`

Recorder opens a local socket and **binds itself** to `store_socket` (no `writer_*` in the result).

On the session socket:

- Messages (+ trailing payload): `debug_snapshot_event`, `debug_io_event`, `debug_logs_event`, `debug_spans_event`, `debug_metrics_event`, `debug_bindings_event`, `debug_registry_event`, `debug_code_event`
- RPC `debug_session`: `set_labels`, `sync`, `seal`, `fail`

One writer; no read/tooling API; backpressure = sockets. Labels are `map<string,string>`. Log/span/metric payloads are opaque bytes. Topology: bindings payload → `bindings.bin`; registry event `{ process_id, socket_id }` + payload → `registry.<pid>.<sock>.bin`; code event → `code.wasm` (truncate overwrite). Session/snap side-cars are typed binary (`meta.bin`, `snap.<uid>.meta.bin`).

## On-disk layout (under ts-fs `rootDir`)

```
sessions/<session_id>/meta.bin
sessions/<session_id>/snap.<uid>.bin
sessions/<session_id>/snap.<uid>.meta.bin
sessions/<session_id>/io.bin|logs.bin|spans.bin|metrics.bin
sessions/<session_id>/bindings.bin
sessions/<session_id>/registry.<pid>.<sock>.bin
sessions/<session_id>/code.wasm
```

## Build

From the repo root with the emscripten toolchain (same as `kernel-shield`):

```bash
cmake -S . -B cmake-build-emscripten -DWASM=ON
cmake --build cmake-build-emscripten --target debug-store
# → cmake-build-emscripten/debug-store.wasm
```

## Tests

```bash
npm install
npm test --workspace=@wa-os/debug-store-tests
```

Harness (`debug-store/ts-test`): Kernel + `createFsProcess` + `ModuleProcess(debug-store.wasm)` + recorder `LocalProcess`. Host binds `fs.0 ↔ store.1` before `begin`. Assertions use `node:fs` under a temp `rootDir`.

Monorepo release gate: [`TESTING.md`](../TESTING.md) → `npm run gate`.

## Contract sources

- Types: [`cpp-rpc/src/examples/structures/debug_store.hpp`](../cpp-rpc/src/examples/structures/debug_store.hpp)
- Codegen stub module: [`cpp-rpc/src/examples/modules/debug_store.cpp`](../cpp-rpc/src/examples/modules/debug_store.cpp)
