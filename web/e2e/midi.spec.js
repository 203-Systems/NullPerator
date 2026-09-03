import { expect, test } from '@playwright/test'
import { readFile } from 'node:fs/promises'
import { stopWorkbench } from './helpers/runtime.js'

async function installMidiHarness(page, initialPermission = 'granted') {
  await page.addInitScript(({ permission }) => {
    const requests = []
    const sends = []
    let permissionState = permission
    let generation = 0

    const makePort = (type, id, name) => {
      const portGeneration = ++generation
      return {
        type, id, name, manufacturer: 'PicoTracker Test',
        state: 'connected', connection: 'closed', onmidimessage: null,
        async open() { this.connection = 'open'; return this },
        async close() { this.connection = 'closed'; return this },
        send(bytes, timestamp) {
          sends.push({
            bytes: Array.from(bytes),
            timestamp: arguments.length > 1 ? timestamp : null,
            calledAt: performance.now(),
            portGeneration,
          })
        },
      }
    }

    let input = makePort('input', 'in-stable', 'Loopback Input')
    let output = makePort('output', 'out-stable', 'Loopback Output')
    const access = {
      inputs: new Map([[input.id, input]]),
      outputs: new Map([[output.id, output]]),
      onstatechange: null,
    }
    Object.defineProperty(navigator, 'requestMIDIAccess', {
      configurable: true,
      value: async (options) => {
        requests.push({ ...options })
        if (permissionState === 'denied') {
          throw new DOMException('MIDI permission denied by test', 'NotAllowedError')
        }
        return access
      },
    })
    globalThis.__midiTest = {
      requestCalls: () => requests.map((request) => ({ ...request })),
      setPermission(value) { permissionState = value },
      injectInput(bytes, timestamp) {
        if (typeof input.onmidimessage !== 'function') return false
        input.onmidimessage({ data: new Uint8Array(bytes), timeStamp: timestamp })
        return true
      },
      sends: () => sends.map((send) => ({ ...send, bytes: [...send.bytes] })),
      async disconnectPorts() {
        input.state = 'disconnected'
        output.state = 'disconnected'
        access.inputs.delete(input.id)
        access.outputs.delete(output.id)
        await access.onstatechange?.({ port: input })
      },
      async reconnectPorts() {
        input = makePort('input', 'in-stable', 'Loopback Input')
        output = makePort('output', 'out-stable', 'Loopback Output')
        access.inputs.set(input.id, input)
        access.outputs.set(output.id, output)
        await access.onstatechange?.({ port: input })
      },
      state: () => ({
        accessHandlerAttached: typeof access.onstatechange === 'function',
        inputHandlerAttached: typeof input.onmidimessage === 'function',
        inputConnection: input.connection,
        outputConnection: output.connection,
      }),
    }
  }, { permission: initialPermission })
}

async function openMidiPanel(page) {
  await expect(page.locator('[data-runtime-state="ready"]')).toBeVisible({ timeout: 20_000 })
  await page.getByRole('button', { name: 'MIDI', exact: true }).click()
  return page.getByRole('region', { name: 'MIDI' })
}

test('MIDI routing is available normally and drop metrics require developer tools', async ({ page }) => {
  await installMidiHarness(page)
  await page.goto('/?audio=disabled&midi-test=1')

  const dashboard = page.locator('.dashboard')
  const panel = await openMidiPanel(page)
  await expect(dashboard).toHaveAttribute('data-developer-mode', 'false')
  await expect(panel).toBeVisible()
  await expect(panel.locator('.phase-badge')).toHaveText('Not connected')
  await expect(panel.getByText('idle', { exact: true })).toHaveCount(0)
  await expect(panel.locator('[aria-label="MIDI diagnostics"]')).toHaveCount(0)

  await page.getByRole('button', { name: 'Connect MIDI' }).click()
  await expect(panel).toHaveAttribute('data-midi-state', 'ready')
  await expect(panel.locator('[aria-label="MIDI diagnostics"]')).toHaveCount(0)

  await page.getByRole('button', { name: 'Settings', exact: true }).click()
  await page.getByRole('button', { name: 'Developer tools', exact: true }).click()
  await expect(dashboard).toHaveAttribute('data-developer-mode', 'true')
  await page.getByRole('button', { name: 'MIDI', exact: true }).click()

  const developerPanel = page.getByRole('region', { name: 'MIDI' })
  await expect(developerPanel.locator('[aria-label="MIDI diagnostics"]')).toBeVisible()
  await expect(developerPanel.getByText('Dropped input bytes', { exact: true })).toBeVisible()
})

test('Web MIDI crosses the WASM input/output queues and survives stable-id reconnects', async ({ page }) => {
  await installMidiHarness(page)
  await page.goto('/?midi-test=1')
  const panel = await openMidiPanel(page)
  await expect(panel).toHaveAttribute('data-midi-state', 'idle')
  await expect.poll(() => page.evaluate(() => globalThis.__midiTest.requestCalls())).toEqual([])
  await expect.poll(() => page.evaluate(() => typeof globalThis.__picoTrackerMidiTest?.snapshot)).toBe('function')

  await page.getByRole('button', { name: 'Connect MIDI' }).click()
  await expect(panel).toHaveAttribute('data-midi-state', 'ready')
  await expect.poll(() => page.evaluate(() => globalThis.__midiTest.requestCalls())).toEqual([{ sysex: false }])
  await page.getByLabel('Input').selectOption('in-stable')
  await page.getByLabel('Output').selectOption('out-stable')
  await expect(panel.locator('label small').filter({ hasText: 'Connected' })).toHaveCount(2)
  await expect.poll(() => page.evaluate(() => globalThis.__midiTest.state().inputHandlerAttached)).toBe(true)

  const beforeInput = await page.evaluate(() => globalThis.__picoTrackerMidiTest.snapshot())
  expect(await page.evaluate(() => globalThis.__midiTest.injectInput([0xB0, 7, 99], 1234.5))).toBe(true)
  await expect.poll(() => page.evaluate((before) => {
    const snapshot = globalThis.__picoTrackerMidiTest.snapshot()
    return {
      processed: snapshot.processedInputBytes - before,
      byte: snapshot.lastInputByte,
      timestamp: snapshot.lastInputTimestamp,
    }
  }, beforeInput.processedInputBytes), { timeout: 15_000 })
    .toEqual({ processed: 3, byte: 99, timestamp: 1234.5 })

  const noteRequestedAt = await page.evaluate(() => performance.now())
  expect(await page.evaluate(() => globalThis.__picoTrackerMidiTest.emitOutput([0x91, 64, 100], 500))).toBe(true)
  await expect.poll(() => page.evaluate(() => globalThis.__midiTest.sends().length)).toBe(1)
  const note = (await page.evaluate(() => globalThis.__midiTest.sends()))[0]
  expect(note.bytes).toEqual([0x91, 64, 100])
  expect(note.timestamp).toBeGreaterThan(noteRequestedAt + 400)
  expect(note.timestamp).toBeLessThan(noteRequestedAt + 800)
  expect(note.calledAt).toBeLessThan(note.timestamp)

  const clockRequestedAt = await page.evaluate(() => performance.now())
  expect(await page.evaluate(() => globalThis.__picoTrackerMidiTest.emitOutput([0xF8], 650))).toBe(true)
  await expect.poll(() => page.evaluate(() => globalThis.__midiTest.sends().length)).toBe(2)
  const clock = (await page.evaluate(() => globalThis.__midiTest.sends()))[1]
  expect(clock.bytes).toEqual([0xF8])
  expect(clock.timestamp).toBeGreaterThan(clockRequestedAt + 550)
  expect(clock.timestamp).toBeLessThan(clockRequestedAt + 950)
  expect(clock.calledAt).toBeLessThan(clock.timestamp)

  const beforeDisconnect = await page.evaluate(() => globalThis.__picoTrackerMidiTest.snapshot())
  await page.evaluate(() => globalThis.__midiTest.disconnectPorts())
  await expect(page.getByText('Waiting for the selected device to reconnect', { exact: true })).toHaveCount(2)
  await expect.poll(() => page.evaluate(() => globalThis.__picoTrackerMidiTest.snapshot().inputResetGeneration))
    .toBeGreaterThan(beforeDisconnect.inputResetGeneration)
  expect(await page.evaluate(() => globalThis.__midiTest.injectInput([0xB0, 10, 55], 2345.75))).toBe(false)
  expect(await page.evaluate(() => globalThis.__picoTrackerMidiTest.emitOutput([0x90, 67, 80], 200))).toBe(true)
  await page.waitForTimeout(300)
  expect(await page.evaluate(() => globalThis.__midiTest.sends().length)).toBe(2)

  await page.evaluate(() => globalThis.__midiTest.reconnectPorts())
  await expect(panel.locator('label small').filter({ hasText: 'Connected' })).toHaveCount(2)
  await expect.poll(() => page.evaluate(() => globalThis.__midiTest.state().inputHandlerAttached)).toBe(true)
  const beforeReconnectInput = await page.evaluate(() => globalThis.__picoTrackerMidiTest.snapshot())
  expect(await page.evaluate(() => globalThis.__midiTest.injectInput([0xB0, 10, 55], 2345.75))).toBe(true)
  await expect.poll(() => page.evaluate((before) => {
    const snapshot = globalThis.__picoTrackerMidiTest.snapshot()
    return [snapshot.processedInputBytes - before, snapshot.lastInputByte, snapshot.lastInputTimestamp]
  }, beforeReconnectInput.processedInputBytes), { timeout: 15_000 })
    .toEqual([3, 55, 2345.75])
  expect(await page.evaluate(() => globalThis.__picoTrackerMidiTest.emitOutput([0x80, 64, 0], 250))).toBe(true)
  await expect.poll(() => page.evaluate(() => globalThis.__midiTest.sends().length)).toBe(3)
  expect((await page.evaluate(() => globalThis.__midiTest.sends()))[2].bytes).toEqual([0x80, 64, 0])

  await stopWorkbench(page)
  await expect(page.locator('[data-runtime-state="idle"]')).toBeVisible({ timeout: 10_000 })
  await expect.poll(() => page.evaluate(() => typeof globalThis.__picoTrackerMidiTest)).toBe('undefined')
  await expect.poll(() => page.evaluate(() => globalThis.__midiTest.state())).toEqual({
    accessHandlerAttached: false,
    inputHandlerAttached: false,
    inputConnection: 'closed',
    outputConnection: 'closed',
  })
})

test('Web MIDI requests permission only on click, reports denial, and can retry', async ({ page }) => {
  await installMidiHarness(page, 'denied')
  await page.goto('/?midi-test=1')
  const panel = await openMidiPanel(page)
  await expect(panel).toHaveAttribute('data-midi-state', 'idle')
  await expect.poll(() => page.evaluate(() => globalThis.__midiTest.requestCalls())).toEqual([])

  await page.getByRole('button', { name: 'Connect MIDI' }).click()
  await expect(panel).toHaveAttribute('data-midi-state', 'denied')
  await expect(panel.getByRole('status')).toContainText('MIDI access was not granted')
  await expect.poll(() => page.evaluate(() => globalThis.__midiTest.requestCalls())).toEqual([{ sysex: false }])

  await page.evaluate(() => globalThis.__midiTest.setPermission('granted'))
  await page.getByRole('button', { name: 'Try MIDI again' }).click()
  await expect(panel).toHaveAttribute('data-midi-state', 'ready')
  await expect.poll(() => page.evaluate(() => globalThis.__midiTest.requestCalls())).toEqual([
    { sysex: false },
    { sysex: false },
  ])
})

test('Web MIDI trace correlates input processing and output browser drain', async ({ page }) => {
  await installMidiHarness(page)
  await page.goto('/?midi-test=1&dev=1')
  const panel = await openMidiPanel(page)
  await page.getByRole('button', { name: 'Connect MIDI' }).click()
  await expect(panel).toHaveAttribute('data-midi-state', 'ready')
  await page.getByLabel('Input').selectOption('in-stable')
  await page.getByLabel('Output').selectOption('out-stable')

  await page.getByRole('button', { name: 'Trace', exact: true }).click()
  await page.getByRole('button', { name: 'Start capture' }).click()
  await page.getByRole('button', { name: 'MIDI', exact: true }).click()

  const beforeInput = await page.evaluate(() =>
    globalThis.__picoTrackerMidiTest.snapshot().processedInputBytes)
  expect(await page.evaluate(() =>
    globalThis.__midiTest.injectInput([0x90, 60, 100], performance.now()))).toBe(true)
  await expect.poll(() => page.evaluate((before) =>
    globalThis.__picoTrackerMidiTest.snapshot().processedInputBytes - before,
  beforeInput)).toBe(3)

  // A one-second future send must still have only the immediate native queue
  // to browser-main hand-off in midi.output_latency_us.
  expect(await page.evaluate(() =>
    globalThis.__picoTrackerMidiTest.emitOutput([0x90, 64, 100], 1_000))).toBe(true)
  await expect.poll(() => page.evaluate(() => globalThis.__midiTest.sends().length)).toBe(1)

  await page.getByRole('button', { name: 'Trace', exact: true }).click()
  await page.getByRole('button', { name: 'Stop capture' }).click()
  const downloadPromise = page.waitForEvent('download')
  await page.getByRole('button', { name: 'Download Chrome JSON' }).click()
  const download = await downloadPromise
  const exported = JSON.parse(await readFile(await download.path(), 'utf8'))

  const inputLatency = exported.traceEvents.find((event) =>
    event.name === 'midi.input_latency_us' && event.ph === 'C')
  const inputAccepted = exported.traceEvents.find((event) =>
    event.name === 'midi.input_accepted' && event.ph === 'i' &&
    event.args.correlation === inputLatency?.args.correlation)
  expect(inputAccepted).toBeTruthy()
  expect(inputLatency).toBeTruthy()
  expect(inputLatency.args.latencyUs).toBeGreaterThanOrEqual(0)

  const outputLatency = exported.traceEvents.find((event) =>
    event.name === 'midi.output_latency_us' && event.ph === 'C')
  const outputQueued = exported.traceEvents.find((event) =>
    event.name === 'midi.output_queued' && event.ph === 'i' &&
    event.args.correlation === outputLatency?.args.correlation)
  expect(outputQueued).toBeTruthy()
  expect(outputLatency).toBeTruthy()
  expect(outputLatency.args.latencyUs).toBeGreaterThanOrEqual(0)
  expect(outputLatency.args.latencyUs).toBeLessThan(2_000_000)

  await stopWorkbench(page)
  await expect(page.locator('[data-runtime-state="idle"]')).toBeVisible({ timeout: 10_000 })
})
