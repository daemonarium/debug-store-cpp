import { readFile } from 'node:fs/promises'
import { join, dirname } from 'node:path'
import { fileURLToPath } from 'node:url'
import os from 'node:os'
import fsp from 'node:fs/promises'

import { Kernel, ModuleProcess, LocalProcess, Direction } from '@daemonarium/kernel'
import {
  SocketRpcClient,
  CodecRegistry,
  registerDynamicCodecs,
  deserializeTypeRegistry,
  deserializeValueByType,
  serializeValueByType,
  deserializeTypedJson,
  ensureRpcCodecs,
  LocalCircularBuffer,
  LocalCircularBufferReader,
  TypeKind,
  type TypeRegistry,
  type SocketOptions,
  type LocalSocket,
} from '@daemonarium/rpc'
import { createFsProcess, type FsProcessHandle } from '@daemonarium/fs'

const __filename = fileURLToPath(import.meta.url)
const __dirname = dirname(__filename)

export const SOCKET_OPTS: SocketOptions = {
  inputBufferSize: 65536,
  outputBufferSize: 65536,
  flushThreshold: 4096,
  dataEventThreshold: 0,
  drainEventThreshold: 4,
  flags: 0,
}

export const DEBUG_MESSAGE_TYPES = [
  'debug_snapshot_event',
  'debug_io_event',
  'debug_logs_event',
  'debug_spans_event',
  'debug_metrics_event',
  'debug_bindings_event',
  'debug_registry_event',
  'debug_code_event',
] as const

export interface TestContext {
  kernel: Kernel
  rootDir: string
  fsHandle: FsProcessHandle
  store: ModuleProcess
  storeProcessId: number
  recorder: LocalProcess
  userClient: SocketRpcClient
  userRegistry: TypeRegistry
  userSocket: LocalSocket
  methodId: (name: string) => number
  cleanup: () => Promise<void>
}

export interface SessionChannel {
  socket: LocalSocket
  client: SocketRpcClient
  registry: TypeRegistry
  methodId: (name: string) => number
  sendPayload: (typeName: string, header: object, payload: Uint8Array) => void
}

function registerMessageCodecsInOrder(
  codecs: CodecRegistry,
  registry: TypeRegistry,
  names: readonly string[],
  required: boolean
): void {
  for (const name of names) {
    if (codecs.messages.has(name)) continue
    const type = registry.getTypeByName(name)
    if (!type || type.kind !== TypeKind.Message) {
      if (required) {
        throw new Error(`missing message type ${name} in registry`)
      }
      continue
    }
    codecs.messages.register({
      name,
      serialize: (writer, value) => serializeValueByType(writer as any, registry, type, value as any),
      deserialize: (reader, output) => deserializeValueByType(reader as any, registry, type, output),
    })
  }
}

export async function withTempRoot<T>(fn: (root: string) => Promise<T>): Promise<T> {
  const root = await fsp.mkdtemp(join(os.tmpdir(), 'waos-debug-store-'))
  try {
    return await fn(root)
  } finally {
    await fsp.rm(root, { recursive: true, force: true })
  }
}

export async function setupTestEnvironment(rootDir: string): Promise<TestContext> {
  const kernel = new Kernel()

  const fsHandle = createFsProcess(kernel, {
    rootDir,
    processId: 2,
    socketId: 0,
    socketOptions: SOCKET_OPTS,
  })

  const wasmPath = join(__dirname, '../../build-web/debug-store.wasm')
  const wasmCode = await readFile(wasmPath)

  const storeProcessId = 1
  const store = new ModuleProcess(kernel, storeProcessId, 2048, 5_000_000)
  await store.setModuleCode(wasmCode)
  await store.start()

  while (kernel.processes.length <= storeProcessId) {
    kernel.processes.push(null)
  }
  kernel.processes[storeProcessId] = store

  // Host binds store.FS (1) ↔ fs.0 before first begin.
  fsHandle.process.bind(store, 0, 1, Direction.BOTH)

  const recorder = new LocalProcess(kernel, 0, 1_000_000)
  kernel.processes[0] = recorder

  const userSocket = recorder.createSocket(0, SOCKET_OPTS)
  recorder.bind(store, 0, 0, Direction.BOTH)

  const rpcDescriptionBytes = store.readRawRpcDescription(0)
  const userRegistry = deserializeTypeRegistry(rpcDescriptionBytes)
  userRegistry.assignMethodIds()
  recorder.setRpcDescription(0, userRegistry)

  const codecs = new CodecRegistry()
  registerDynamicCodecs(codecs, userRegistry)
  ensureRpcCodecs(codecs, userRegistry)
  registerMessageCodecsInOrder(codecs, userRegistry, DEBUG_MESSAGE_TYPES, false)

  const userClient = new SocketRpcClient(userSocket, userRegistry, codecs)

  const methods = new Map(
    userRegistry.getMethods().map((m) => [m.memberName, m.methodId])
  )
  const methodId = (name: string) => {
    const full = name.includes('.') ? name : `debug_store.${name}`
    const id = methods.get(full)
    if (id === undefined) {
      throw new Error(`missing method ${full}; have: ${[...methods.keys()].join(', ')}`)
    }
    return id
  }

  return {
    kernel,
    rootDir,
    fsHandle,
    store,
    storeProcessId,
    recorder,
    userClient,
    userRegistry,
    userSocket,
    methodId,
    cleanup: async () => {
      await store.kill(undefined)
    },
  }
}

export async function openSessionChannel(
  ctx: TestContext,
  storeSocket: number
): Promise<SessionChannel> {
  const localSocketId = 10
  const socket = ctx.recorder.createSocket(localSocketId, SOCKET_OPTS)
  ctx.recorder.bind(ctx.store, localSocketId, storeSocket, Direction.BOTH)

  const rpcDescriptionBytes = ctx.store.readRawRpcDescription(storeSocket)
  const registry = deserializeTypeRegistry(rpcDescriptionBytes)
  registry.assignMethodIds()
  ctx.recorder.setRpcDescription(localSocketId, registry)

  const codecs = new CodecRegistry()
  registerDynamicCodecs(codecs, registry)
  ensureRpcCodecs(codecs, registry)
  registerMessageCodecsInOrder(codecs, registry, DEBUG_MESSAGE_TYPES, true)

  const client = new SocketRpcClient(socket, registry, codecs)
  for (const name of DEBUG_MESSAGE_TYPES) {
    client.registerMessageType(name)
  }

  const methods = new Map(
    registry.getMethods().map((m) => [m.memberName, m.methodId])
  )
  const methodId = (name: string) => {
    const full = name.includes('.') ? name : `debug_session.${name}`
    const id = methods.get(full)
    if (id === undefined) {
      throw new Error(`missing method ${full}; have: ${[...methods.keys()].join(', ')}`)
    }
    return id
  }

  const sendPayload = (typeName: string, header: object, payload: Uint8Array) => {
    const channel = client.channel()
    const writer = channel.beginSend(typeName, header)
    for (let i = 0; i < payload.length; i++) {
      writer.writeU8(payload[i])
    }
    channel.commitSend()
  }

  return { socket, client, registry, methodId, sendPayload }
}

export function deserializeTypedBytes(
  registry: TypeRegistry,
  typeName: string,
  bytes: Uint8Array
): Record<string, unknown> {
  const codecs = new CodecRegistry()
  registerDynamicCodecs(codecs, registry)
  ensureRpcCodecs(codecs, registry)
  const buffer = new LocalCircularBuffer(Math.max(bytes.length + 256, 1024))
  buffer.write(bytes, bytes.length)
  const reader = new LocalCircularBufferReader(buffer, codecs)
  return deserializeTypedJson(reader, registry, typeName) as Record<string, unknown>
}
