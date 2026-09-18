import { afterEach, describe, expect, it, vi } from 'vitest'
import { readFileSync } from 'node:fs'
import { runInNewContext } from 'node:vm'
import { createNativeRuntimeManager } from '../src/stores/nativeRuntime.js'
const bootstrap = readFileSync(new URL('../../android/app/src/main/native-bootstrap.js', import.meta.url), 'utf8')
afterEach(() => { vi.unstubAllGlobals(); vi.useRealTimers() })
describe('Android native bridge', () => {
  it('correlates asynchronous replies, rejects errors and expires missing replies', async () => {
    vi.useFakeTimers()
    const messages = []
    const context = { setTimeout, clearTimeout, NullPeratorAndroid: { postMessage: text => messages.push(JSON.parse(text)) } }
    runInNewContext(bootstrap, context)
    expect(context.__nullPeratorNativeCore).toBe(true)
    const first = context.__nullPeratorNativeTransport.postMessage({ command: 'nativeFrame', after: 3 })
    const second = context.__nullPeratorNativeTransport.postMessage({ command: 'nativeReady' })
    context.__nullPeratorAndroidReply(messages[1].id, { platform: 'android' }, null)
    await expect(second).resolves.toMatchObject({ platform: 'android' })
    context.__nullPeratorAndroidReply(messages[0].id, null, 'Frame failed')
    await expect(first).rejects.toThrow('Frame failed')
    const missing = context.__nullPeratorNativeTransport.postMessage({ command: 'nativeReady' })
    const assertion = expect(missing).rejects.toThrow('timed out')
    await vi.advanceTimersByTimeAsync(15000)
    await assertion
    expect(vi.getTimerCount()).toBe(0)
  })
  it('uses Android transport for the existing runtime input contract', async () => {
    const postMessage = vi.fn(async message => message.command === 'nativeReady' ? { platform: 'android' } : true)
    vi.stubGlobal('__nullPeratorNativeTransport', { postMessage })
    vi.stubGlobal('__nullPeratorNativePlatform', 'android')
    const manager = createNativeRuntimeManager()
    await manager.start()
    expect(manager.getSnapshot().buildMetadata.platform).toBe('android')
    manager.getSnapshot().input.pressAction(6)
    manager.getSnapshot().input.releaseAction(6)
    expect(postMessage).toHaveBeenCalledWith({ command: 'nativeAction', action: 6, pressed: true, repeat: false })
    expect(postMessage).toHaveBeenCalledWith({ command: 'nativeAction', action: 6, pressed: false, repeat: false })
  })
  it('keeps the iOS WebKit transport working', async () => {
    const postMessage = vi.fn(async () => ({ platform: 'ios' }))
    vi.stubGlobal('webkit', { messageHandlers: { nullPeratorNative: { postMessage } } })
    const manager = createNativeRuntimeManager()
    await manager.start()
    expect(manager.getSnapshot().buildMetadata.platform).toBe('ios')
    expect(postMessage).toHaveBeenCalledWith({ command: 'nativeReady' })
  })
})
