# debug-store

A WebAssembly module that stores one debug session. The host binds
socket 1 to the filesystem RPC. This module does not open the disk,
and it is not part of the kernel.

This repository is `daemonarium/debug-store-cpp`. The artifact is
`debug-store.wasm`. It is not an npm package.

## Why it is a separate module

The kernel stays off the host disk. `@daemonarium/fs` is the Node
sink for that reason. This module is the portable writer: socket 1
speaks that same RPC, so a later browser sink can replace the Node
host without a new module.

`kernel-shield` and `recorder` are other repositories. Recording
does not move into the kernel.

## Build

Stock Clang cannot compile this module. Build the
[reflection-toolchain](https://github.com/daemonarium/reflection-toolchain)
sysroot and point `P2996_COMPILER_SYSROOT` at it.

This module compiles sources from
[rpc-cpp](https://github.com/daemonarium/rpc-cpp) `v0.1.0`:

```
git submodule add https://github.com/daemonarium/rpc-cpp.git extern/rpc-cpp
cd extern/rpc-cpp && git checkout v0.1.0
```

```
cmake -B build-web -S . \
  -DCMAKE_TOOLCHAIN_FILE=$P2996_COMPILER_SYSROOT/toolchain/emscripten-p2996-toolchain.cmake \
  -DWASM=ON
cmake --build build-web --target debug-store
```

`build-web/debug-store.wasm` exports `_rpc_description`, `_start`,
`_malloc`, `_waos_socket_data`, and `_waos_socket_output_drain`.

## How a host runs it

`@daemonarium/kernel` loads the wasm bytes. `@daemonarium/fs` owns
`rootDir`. Bind store socket 1 to the fs socket before the first
`begin`. A recorder then calls `debug_store.begin` on socket 0 and
binds itself to the session socket the result names.

See [docs/examples.md](docs/examples.md).

## Sockets

| Socket | Role |
|--------|------|
| 0 | User API. `debug_store.begin` arrives here. |
| 1 | Filesystem RPC. The host binds this before `begin`. |
| 2 and up | One socket per session. |

The on-disk layout, message names, and session methods are in
[docs/architecture.md](docs/architecture.md).

## License

MIT. Upstream notices are in `NOTICE`. Conduct is in
`CODE_OF_CONDUCT.md`.
