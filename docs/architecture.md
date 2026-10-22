# Architecture

The contract header is
[rpc-cpp](https://github.com/daemonarium/rpc-cpp) `v0.1.0`,
`src/examples/structures/debug_store.hpp`. USER, session, and the
filesystem client use flat message framing (`flat_socket_rpc_*`).
This module does not use compressed module RPC.

```
Recorder
    │  debug_store.begin on socket 0
    │  then bind to the session socket
    ▼
debug-store.wasm
    │  filesystem RPC on socket 1
    ▼
@daemonarium/fs
    ▼
rootDir
```

## begin

`debug_store.begin({ name, process_id, labels })` returns
`{ session_id, store_process, store_socket }`.

The caller opens a local socket and binds itself to `store_socket`.
The result has no writer handle. One writer. There is no read API.
Backpressure is the socket.

## Session socket

Messages, each with a trailing payload:

- `debug_snapshot_event`
- `debug_io_event`
- `debug_logs_event`
- `debug_spans_event`
- `debug_metrics_event`
- `debug_bindings_event`
- `debug_registry_event`
- `debug_code_event`

RPC `debug_session`: `set_labels`, `sync`, `seal`, `fail`.

Labels are `map<string, string>`. Log, span, and metric payloads
are opaque bytes.

## On disk

Under the fs `rootDir`:

```
sessions/<session_id>/meta.bin
sessions/<session_id>/snap.<uid>.bin
sessions/<session_id>/snap.<uid>.meta.bin
sessions/<session_id>/io.bin
sessions/<session_id>/logs.bin
sessions/<session_id>/spans.bin
sessions/<session_id>/metrics.bin
sessions/<session_id>/bindings.bin
sessions/<session_id>/registry.<pid>.<sock>.bin
sessions/<session_id>/code.wasm
```

`meta.bin` and `snap.<uid>.meta.bin` are typed binary. A bindings
payload becomes `bindings.bin`. A registry event
`{ process_id, socket_id }` plus its payload becomes
`registry.<pid>.<sock>.bin`. A code event overwrites `code.wasm`.

## Filesystem method ids

`src/fs_rpc.hpp` calls the host by number, not by name:

| Call | Id |
|------|----|
| mkdir | 0 |
| open | 7 |
| close | 8 |
| write | 9 |
| sync | 12 |

Those ids are the member order of `@daemonarium/fs` 0.1.0. Do not
renumber them against a different registry.

## Not this package

- `recorder` is the next repository. It calls this module and `kernel-shield`.
- `kernel-shield` is not linked here.
- A native writer can replace the wasm binary later. The RPC stays.
- A browser filesystem sink can implement the same FS RPC. This module does not change for that.
