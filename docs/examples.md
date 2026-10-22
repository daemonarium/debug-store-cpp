# Example

The host chooses `rootDir` and supplies the wasm bytes. Bind the
filesystem socket before `begin`.

```ts
import { readFile } from 'node:fs/promises'
import { Kernel, ModuleProcess, LocalProcess, Direction } from '@daemonarium/kernel'
import {
  CodecRegistry,
  SocketRpcClient,
  deserializeTypeRegistry,
  ensureRpcCodecs,
  registerDynamicCodecs,
} from '@daemonarium/rpc'
import { createFsProcess } from '@daemonarium/fs'

const socketOptions = {
  inputBufferSize: 65536,
  outputBufferSize: 65536,
  flushThreshold: 4096,
  dataEventThreshold: 0,
  drainEventThreshold: 4,
  flags: 0,
}

const kernel = new Kernel()
const fs = createFsProcess(kernel, {
  rootDir: './data',
  processId: 2,
  socketId: 0,
  socketOptions,
})

const store = new ModuleProcess(kernel, 1, 2048, 5_000_000)
await store.setModuleCode(await readFile('build-web/debug-store.wasm'))
await store.start()
kernel.processes[1] = store

// fs socket 0 ↔ store socket 1, before the first begin
fs.process.bind(store, 0, 1, Direction.BOTH)

const recorder = new LocalProcess(kernel, 0, 1_000_000)
kernel.processes[0] = recorder
const userSocket = recorder.createSocket(0, socketOptions)
recorder.bind(store, 0, 0, Direction.BOTH)

const registry = deserializeTypeRegistry(store.readRawRpcDescription(0))
registry.assignMethodIds()
recorder.setRpcDescription(0, registry)

const codecs = new CodecRegistry()
registerDynamicCodecs(codecs, registry)
ensureRpcCodecs(codecs, registry)
const rpc = new SocketRpcClient(userSocket, registry, codecs)

const begin = registry.getMethods().find((m) => m.memberName === 'debug_store.begin')
const session = await rpc.call(begin!.methodId, {
  name: 'run-1',
  process_id: 7n,
  labels: { env: 'dev' },
})
// session.store_socket is the socket the recorder binds next
```

`./data/sessions/1/meta.bin` is the session file on the host. A
process that is not bound to socket 0 cannot call `begin`.
