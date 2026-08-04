import { expect } from 'chai'
import fsp from 'node:fs/promises'
import { join } from 'node:path'
import { RpcException } from '@wa-os/ts-rpc'
import {
  openSessionChannel,
  setupTestEnvironment,
  withTempRoot,
} from './setup.js'

function num(v: unknown): number {
  if (typeof v === 'bigint') return Number(v)
  return Number(v)
}

describe('debug-store WASM', () => {
  it('begin creates session meta on disk', async () => {
    await withTempRoot(async (root) => {
      const ctx = await setupTestEnvironment(root)
      try {
        const result = await ctx.userClient.call(ctx.methodId('begin'), {
          name: 'unit-begin',
          process_id: 42n,
          labels: { env: 'test' },
        }) as Record<string, unknown>

        expect(num(result.session_id)).to.equal(1)
        expect(num(result.store_process)).to.equal(ctx.storeProcessId)
        expect(num(result.store_socket)).to.be.at.least(2)

        const metaPath = join(root, 'sessions', '1', 'meta.json')
        const meta = JSON.parse(await fsp.readFile(metaPath, 'utf8'))
        expect(meta.name).to.equal('unit-begin')
        expect(meta.process_id).to.equal(42)
        expect(meta.status).to.equal('open')
        expect(meta.labels.env).to.equal('test')
      } finally {
        await ctx.cleanup()
      }
    })
  })

  it('records snapshot/io/logs payloads and seals', async () => {
    await withTempRoot(async (root) => {
      const ctx = await setupTestEnvironment(root)
      try {
        const begin = await ctx.userClient.call(ctx.methodId('begin'), {
          name: 'e2e',
          process_id: 7n,
          labels: { run: '1' },
        }) as Record<string, unknown>

        const sessionId = num(begin.session_id)
        const storeSocket = num(begin.store_socket)
        const session = await openSessionChannel(ctx, storeSocket)

        const snapPayload = new TextEncoder().encode('SNAP-BYTES')
        session.sendPayload('debug_snapshot_event', {
          snapshot_uid: 99n,
          mana_used: 10n,
          mana_limit: 1000n,
        }, snapPayload)

        const ioPayload = new Uint8Array([1, 2, 3, 4])
        session.sendPayload('debug_io_event', { reserved: 0 }, ioPayload)

        const logsPayload = new TextEncoder().encode('log-line\n')
        session.sendPayload('debug_logs_event', { count: 1 }, logsPayload)

        await session.client.call(session.methodId('sync'))
        await session.client.call(session.methodId('seal'))

        const dir = join(root, 'sessions', String(sessionId))
        const snap = await fsp.readFile(join(dir, 'snap.99.bin'))
        expect(Buffer.from(snap).equals(Buffer.from(snapPayload))).to.equal(true)

        const snapMeta = JSON.parse(await fsp.readFile(join(dir, 'snap.99.meta.json'), 'utf8'))
        expect(snapMeta.snapshot_uid).to.equal(99)
        expect(snapMeta.memory_size).to.equal(snapPayload.length)

        expect(Buffer.from(await fsp.readFile(join(dir, 'io.bin'))).equals(Buffer.from(ioPayload))).to.equal(true)
        expect(Buffer.from(await fsp.readFile(join(dir, 'logs.bin'))).equals(Buffer.from(logsPayload))).to.equal(true)

        const meta = JSON.parse(await fsp.readFile(join(dir, 'meta.json'), 'utf8'))
        expect(meta.status).to.equal('sealed')
      } finally {
        await ctx.cleanup()
      }
    })
  })

  it('double seal raises debug_conflict', async () => {
    await withTempRoot(async (root) => {
      const ctx = await setupTestEnvironment(root)
      try {
        const begin = await ctx.userClient.call(ctx.methodId('begin'), {
          name: 'seal-twice',
          process_id: 1n,
          labels: {},
        }) as Record<string, unknown>
        const session = await openSessionChannel(ctx, num(begin.store_socket))

        await session.client.call(session.methodId('seal'))
        try {
          await session.client.call(session.methodId('seal'))
          expect.fail('expected conflict on second seal')
        } catch (error) {
          expect(error).to.be.instanceOf(RpcException)
          expect((error as RpcException).typeName).to.equal('debug_conflict')
        }
      } finally {
        await ctx.cleanup()
      }
    })
  })

  it('sync after seal raises debug_closed', async () => {
    await withTempRoot(async (root) => {
      const ctx = await setupTestEnvironment(root)
      try {
        const begin = await ctx.userClient.call(ctx.methodId('begin'), {
          name: 'closed-sync',
          process_id: 1n,
          labels: {},
        }) as Record<string, unknown>
        const session = await openSessionChannel(ctx, num(begin.store_socket))

        await session.client.call(session.methodId('seal'))
        try {
          await session.client.call(session.methodId('sync'))
          expect.fail('expected closed on sync after seal')
        } catch (error) {
          expect(error).to.be.instanceOf(RpcException)
          expect((error as RpcException).typeName).to.equal('debug_closed')
        }
      } finally {
        await ctx.cleanup()
      }
    })
  })

  it('empty begin name raises debug_invalid_argument', async () => {
    await withTempRoot(async (root) => {
      const ctx = await setupTestEnvironment(root)
      try {
        try {
          await ctx.userClient.call(ctx.methodId('begin'), {
            name: '',
            process_id: 1n,
            labels: {},
          })
          expect.fail('expected invalid argument')
        } catch (error) {
          expect(error).to.be.instanceOf(RpcException)
          expect((error as RpcException).typeName).to.equal('debug_invalid_argument')
        }
      } finally {
        await ctx.cleanup()
      }
    })
  })
})
