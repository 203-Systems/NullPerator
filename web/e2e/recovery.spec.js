import { expect, test } from '@playwright/test'
import { readFile } from 'node:fs/promises'
import { unzipSync } from 'fflate'

const persistenceMarkerPath = '/data/idbfs-recovery-marker.dat'
const persistenceMarkerBytes = [0x50, 0x54, 0x52, 0x59, 0x01, 0xfe]
const fatalMarkerPath = '/data/runtime-fatal-recovery-marker.dat'
const fatalMarkerBytes = [0x46, 0x41, 0x54, 0x41, 0x4c, 0x02, 0xfd]

test('missing cross-origin isolation shows exact headers and never starts WASM', async ({ page }) => {
  await page.addInitScript(() => {
    Object.defineProperty(globalThis, 'crossOriginIsolated', { configurable: true, value: false })
  })
  await page.goto('/')
  const recovery = page.locator('[data-recovery-kind="isolation"]')
  await expect(recovery).toBeVisible()
  await expect(recovery).toContainText('Cross-Origin-Opener-Policy: same-origin')
  await expect(recovery).toContainText('Cross-Origin-Embedder-Policy: require-corp')
  await expect(page.locator('[data-runtime-state="failed"]')).toBeVisible()
  await expect(page.locator('#picotracker-canvas')).toHaveAttribute('data-frame-content', 'unavailable')
})

test('one-shot WASM load failure is visible and retry boots without clearing settings', async ({ page }) => {
  await page.addInitScript(() => {
    if (!sessionStorage.getItem('picotracker-recovery-settings-seeded')) {
      localStorage.setItem('picotracker.wasm.settings.v1', JSON.stringify({
        version: 1, displayScale: '2', audioBufferFrames: 2048,
        outputVolume: 70, traceMask: 1023, lowLatencyAudio: false,
      }))
      sessionStorage.setItem('picotracker-recovery-settings-seeded', '1')
    }
  })
  await page.goto('/?runtime-fail-test=1')
  const recovery = page.locator('[data-recovery-kind="runtime"]')
  await expect(recovery).toBeVisible({ timeout: 20_000 })
  await expect(recovery).toContainText('Injected one-shot WASM load failure')
  await page.getByRole('button', { name: 'Retry runtime' }).click()
  await expect(page.locator('[data-runtime-state="ready"]')).toBeVisible({ timeout: 20_000 })

  await page.getByRole('button', { name: 'Settings', exact: true }).click()
  await expect(page.getByLabel('Device scale')).toHaveValue('2')
  await expect(page.getByLabel('Target buffer')).toHaveValue('2048')
  await expect(page.getByRole('slider', { name: 'Output volume' })).toHaveValue('70')
  await page.reload()
  await expect(page.locator('[data-runtime-state="ready"]')).toBeVisible({ timeout: 20_000 })
  await page.getByRole('button', { name: 'Settings', exact: true }).click()
  await expect(page.getByRole('slider', { name: 'Output volume' })).toHaveValue('70')
})

test('one-shot IDBFS save failure keeps export available and retry persists exact bytes', async ({ page }) => {
  test.setTimeout(75_000)
  await page.goto('/?storage-test=1')
  await expect(page.locator('[data-runtime-state="ready"]')).toBeVisible({ timeout: 20_000 })

  const failure = await page.evaluate(async ({ path, bytes }) => {
    const storage = globalThis.__picoTrackerStorageTest
    storage.write(path, bytes)
    storage.failNextSync('Injected IDBFS quota failure')
    try {
      await storage.flush()
      return null
    } catch (error) {
      return error instanceof Error ? error.message : String(error)
    }
  }, { path: persistenceMarkerPath, bytes: persistenceMarkerBytes })
  expect(failure).toBe('Injected IDBFS quota failure')

  await page.getByRole('button', { name: 'Files', exact: true }).click()
  const persistence = page.locator('.files-panel .file-sync[data-storage-state="failed"]')
  await expect(persistence).toBeVisible()
  await expect(persistence).toHaveAttribute('data-storage-dirty', 'true')
  await expect(persistence).toContainText('Persistence failed: Injected IDBFS quota failure')

  const exportButton = page.getByRole('button', { name: 'Export ZIP' })
  await expect(exportButton).toBeEnabled()
  const exported = page.waitForEvent('download')
  await exportButton.click()
  const download = await exported
  expect(download.suggestedFilename()).toBe('picotracker-data.zip')
  const downloadPath = await download.path()
  expect(downloadPath).not.toBeNull()
  const archive = unzipSync(new Uint8Array(await readFile(downloadPath)))
  expect(Array.from(archive['idbfs-recovery-marker.dat'])).toEqual(persistenceMarkerBytes)

  await page.getByRole('button', { name: 'Retry save' }).click()
  const persisted = page.locator('.files-panel .file-sync[data-storage-state="ready"]')
  await expect(persisted).toBeVisible()
  await expect(persisted).toHaveAttribute('data-storage-dirty', 'false')
  await expect.poll(() => page.evaluate(
    (path) => globalThis.__picoTrackerStorageTest.read(path),
    persistenceMarkerPath,
  )).toEqual(persistenceMarkerBytes)

  await page.reload()
  await expect(page.locator('[data-runtime-state="ready"]')).toBeVisible({ timeout: 20_000 })
  await expect.poll(() => page.evaluate(
    (path) => globalThis.__picoTrackerStorageTest.read(path),
    persistenceMarkerPath,
  )).toEqual(persistenceMarkerBytes)
})

test('live C++ fatal drains an in-flight mutation before restart', async ({ page }) => {
  test.setTimeout(75_000)
  await page.goto('/?runtime-test=1&storage-test=1')
  await expect(page.locator('[data-runtime-state="ready"]')).toBeVisible({ timeout: 20_000 })

  await page.evaluate(({ path, bytes }) => {
    globalThis.__picoTrackerStorageTest.writePendingMutation(path, bytes)
    globalThis.__picoTrackerRuntimeTest.failCpp()
  }, { path: fatalMarkerPath, bytes: fatalMarkerBytes })

  const recovery = page.locator('[data-recovery-kind="runtime"]')
  await expect(recovery).toBeVisible({ timeout: 20_000 })
  await expect(recovery).toContainText('Injected C++ fatal error')

  await page.getByRole('button', { name: 'Retry runtime' }).click()
  await expect(page.locator('[data-runtime-state="ready"]')).toBeVisible({ timeout: 20_000 })
  await expect.poll(() => page.evaluate(
    (path) => globalThis.__picoTrackerStorageTest.read(path),
    fatalMarkerPath,
  )).toEqual(fatalMarkerBytes)

  await page.reload()
  await expect(page.locator('[data-runtime-state="ready"]')).toBeVisible({ timeout: 20_000 })
  await expect.poll(() => page.evaluate(
    (path) => globalThis.__picoTrackerStorageTest.read(path),
    fatalMarkerPath,
  )).toEqual(fatalMarkerBytes)
})
