import { expect, test } from '@playwright/test'
import { readFile } from 'node:fs/promises'

test('captures live native scopes, benchmarks deterministically, and exports Chrome Trace JSON', async ({ page }) => {
  await page.goto('/')
  await expect(page.locator('[data-runtime-state="ready"]')).toBeVisible({ timeout: 20_000 })
  await page.getByRole('button', { name: 'Trace', exact: true }).click()
  const panel = page.locator('[data-trace-state]')
  await expect(panel).toHaveAttribute('data-trace-state', 'idle')

  await page.getByLabel('Blocks').fill('32')
  await page.getByLabel('Deadline µs').fill('1000000')

  // The disabled and enabled paths must render byte-identical PCM. The bridge
  // treats the fixed fixture hash as an ABI contract rather than accepting any
  // merely non-zero value.
  await page.getByRole('button', { name: 'Run benchmark' }).click()
  await expect(page.getByText('Benchmark complete', { exact: true })).toBeVisible()
  await expect(page.getByText('Fixture v1 · 8 channels · 64 rows · 128-frame stereo blocks')).toBeVisible()
  const fixtureHash = page.getByText('0xc45e4b1c', { exact: true })
  await expect(fixtureHash).toBeVisible()

  await page.getByRole('button', { name: 'Start capture' }).click()
  await expect(panel).toHaveAttribute('data-trace-state', 'capturing')
  // DevicePanel correctly disables its global tracker keys while its section
  // is hidden. Keep the capture alive in the runtime store, visit the visible
  // Device, and hold across an application frame so both native transitions
  // are observed before returning to Trace.
  await page.getByRole('button', { name: 'Device', exact: true }).click()
  await page.locator('#picotracker-canvas').focus()
  // Move down from the initial first row so the accepted press deterministically
  // dirties the C++ UI and produces a committed frame.
  await page.keyboard.down('s')
  await page.waitForTimeout(50)
  await page.keyboard.up('s')
  await page.waitForTimeout(50)
  await page.getByRole('button', { name: 'Trace', exact: true }).click()
  await expect(panel).toHaveAttribute('data-trace-state', 'capturing')
  await page.getByLabel('Blocks').fill('32')
  await page.getByLabel('Deadline µs').fill('1000000')
  await page.getByRole('button', { name: 'Run benchmark' }).click()
  await expect(page.getByText('Benchmark complete', { exact: true })).toBeVisible()
  await expect(fixtureHash).toBeVisible()

  // Let requestAnimationFrame drain several real Frame/ClockTick scopes from
  // the application pthread before finalizing the capture.
  await page.waitForTimeout(250)
  await page.getByRole('button', { name: 'Stop capture' }).click()
  await expect(panel).toHaveAttribute('data-trace-state', 'stopped')
  await expect(panel.getByText(/[1-9][0-9]* events \/ [0-9]+ capacity/)).toBeVisible()
  const durationMs = Number(await panel.locator('[data-trace-duration-ms]').getAttribute('data-trace-duration-ms'))
  expect(durationMs).toBeGreaterThanOrEqual(200)

  const downloadPromise = page.waitForEvent('download')
  await page.getByRole('button', { name: 'Download Chrome JSON' }).click()
  const download = await downloadPromise
  const exported = JSON.parse(await readFile(await download.path(), 'utf8'))
  expect(exported.displayTimeUnit).toBe('ms')
  expect(exported.metadata.benchmark.fixtureHash).toBe(0xc45e4b1c)
  expect(exported.metadata.benchmark.sampleCount).toBe(32)
  expect(exported.metadata.benchmarkFixtureGolden32).toBe(0xc45e4b1c)
  expect(exported.metadata.build).toMatchObject({
    commit: expect.any(String), builtAt: expect.any(String), emscripten: expect.any(String),
  })
  expect(exported.metadata.captureDurationMs).toBeGreaterThanOrEqual(200)
  expect(exported.metadata.recordCount).toBeGreaterThan(0)
  expect(exported.metadata.recordEndUs).toBeGreaterThanOrEqual(exported.metadata.recordStartUs)
  expect(exported.metadata.recordDurationUs).toBeGreaterThanOrEqual(0)
  expect(exported.metadata.recordTimeUnit).toBe('us')
  expect(exported.metadata.captureTimeUnit).toBe('ms')
  expect(exported.traceEvents.some((event) => event.ph === 'M' && event.name === 'thread_name')).toBe(true)
  expect(exported.traceEvents.some((event) => event.name === 'frame' && event.ph === 'B')).toBe(true)
  expect(exported.traceEvents.some((event) => event.name === 'frame' && event.ph === 'E')).toBe(true)
  expect(exported.traceEvents.some((event) => event.cat === 'input' && event.name === 'input.dispatch')).toBe(true)
  const inputLatency = exported.traceEvents.find((event) => event.name === 'input.to_frame_latency_us' && event.ph === 'C')
  const acceptedInput = exported.traceEvents.find((event) => event.name === 'input.accepted' && event.ph === 'i' && event.args.correlation === inputLatency?.args.correlation)
  const presentedInput = exported.traceEvents.find((event) => event.name === 'input.presented' && event.ph === 'i' && event.args.correlation === inputLatency?.args.correlation)
  expect(acceptedInput).toBeTruthy()
  expect(presentedInput).toBeTruthy()
  expect(inputLatency).toBeTruthy()
  expect(inputLatency.args.latencyUs).toBeGreaterThanOrEqual(0)
  expect(inputLatency.ts).toBeGreaterThanOrEqual(acceptedInput.ts)
  expect(exported.traceEvents.some((event) => event.cat === 'midi' && event.name === 'midi.poll')).toBe(true)
  for (const name of [
    'audio.snapshot', 'audio.callback_count', 'audio.underrun_frames',
    'audio.overrun_frames', 'audio.render_duration_us',
    'audio.callback_duration_us', 'audio.callback_max_duration_us',
    'audio.callback_deadline_us',
    'audio.callback_processing_deadline_misses',
  ]) {
    expect(exported.traceEvents.some((event) => event.cat === 'audio' && event.name === name && event.ph === 'C')).toBe(true)
  }
  const benchmarkEvents = exported.traceEvents.filter((event) => event.name === 'benchmark.block')
  expect(benchmarkEvents.filter((event) => event.ph === 'B')).toHaveLength(32)
  expect(benchmarkEvents.filter((event) => event.ph === 'E')).toHaveLength(32)
  expect(new Set(benchmarkEvents.map((event) => event.args.value))).toEqual(new Set(Array.from({ length: 32 }, (_, index) => index)))
  for (const event of exported.traceEvents.filter((entry) => ['B', 'E', 'i', 'C'].includes(entry.ph))) {
    expect(event.args).toMatchObject({
      value: expect.any(Number), sequence: expect.any(Number),
      generation: expect.any(Number), flags: expect.any(Number),
    })
  }

  await page.getByRole('button', { name: 'Stop runtime' }).click()
  await expect(page.locator('[data-runtime-state="idle"]')).toBeVisible({ timeout: 10_000 })
})
