import { expect, test } from '@playwright/test'
import { zipSync, strToU8 } from 'fflate'
import { restartWorkbench, stopWorkbench } from './helpers/runtime.js'

async function clearDisk(page) {
  await page.goto('/oracle.html')
  await page.evaluate(async () => {
    await new Promise((resolve, reject) => {
      const request = indexedDB.deleteDatabase('/data')
      request.onsuccess = () => resolve()
      request.onerror = () => reject(request.error)
      request.onblocked = () => reject(new Error('IDBFS database remained open during cleanup'))
    })
  })
}

test('Files panel uploads, drops, renames, downloads, exports, restores, and persists synthetic data', async ({ page }) => {
  test.setTimeout(90_000)
  await clearDisk(page)
  await page.goto('/?storage-test=1')
  await expect(page.locator('[data-runtime-state="ready"]')).toBeVisible({ timeout: 20_000 })
  await page.evaluate(async () => {
    const storage = globalThis.__picoTrackerStorageTest
    storage.write('/data/.config.xml', [0x3c, 0x2f, 0x3e])
    await storage.flush()
  })
  await page.getByRole('button', { name: 'Files', exact: true }).click()
  await expect(page.getByRole('heading', { name: 'Files' })).toBeVisible()
  await expect(page.getByText('.config.xml', { exact: true })).toHaveCount(0)

  await page.getByRole('button', { name: 'Upload files' }).click()
  await page.locator('input[type="file"][multiple]').setInputFiles([
    { name: 'tone.wav', mimeType: 'audio/wav', buffer: Buffer.from([0x52, 0x49, 0x46, 0x46]) },
    { name: 'project.dat', mimeType: 'application/octet-stream', buffer: Buffer.from([0x50, 0x54, 0x39]) },
    { name: '.samples', mimeType: 'application/octet-stream', buffer: Buffer.from([0x01]) },
    { name: '.picotracker-copy-temp-ABC', mimeType: 'application/octet-stream', buffer: Buffer.from([0x02]) },
  ])
  await expect(page.getByText('tone.wav', { exact: true })).toBeVisible()
  await expect(page.getByText('project.dat', { exact: true })).toBeVisible()
  await expect(page.getByText('.samples', { exact: true })).toBeVisible()
  await expect(page.getByText('.picotracker-copy-temp-ABC', { exact: true })).toBeVisible()

  // A directory remains user-manageable even when its leaf is a valid
  // transaction-file spelling.
  page.once('dialog', (dialog) => dialog.accept('.picotracker-copy-temp-4142'))
  await page.getByRole('button', { name: 'New folder' }).click()
  await expect(page.getByRole('button', { name: '.picotracker-copy-temp-4142', exact: true })).toBeVisible()

  // User-owned dotfiles remain manageable in both modes. Developer tools add
  // diagnostics; they do not unlock file capabilities.
  await page.getByRole('button', { name: 'Settings', exact: true }).click()
  await page.getByRole('button', { name: 'Developer tools', exact: true }).click()
  await page.getByRole('button', { name: 'Files', exact: true }).click()
  await expect(page.getByText('.samples', { exact: true })).toBeVisible()
  await page.getByRole('button', { name: 'Settings', exact: true }).click()
  await page.getByRole('button', { name: 'Developer tools', exact: true }).click()
  await page.getByRole('button', { name: 'Files', exact: true }).click()
  await expect(page.getByText('.samples', { exact: true })).toBeVisible()

  page.once('dialog', (dialog) => dialog.accept('projects'))
  await page.getByRole('button', { name: 'New folder' }).click()
  await expect(page.getByRole('button', { name: 'projects', exact: true })).toBeVisible()
  await page.getByRole('button', { name: 'projects', exact: true }).click()
  await page.evaluate(async () => {
    const storage = globalThis.__picoTrackerStorageTest
    storage.write('/data/projects/.load-rollback.dat', [0x50, 0x54, 0x39])
    await storage.flush()
  })
  await page.getByRole('button', { name: 'Files root' }).click()
  await page.getByRole('button', { name: 'projects', exact: true }).click()
  await expect(page.getByText('.load-rollback.dat', { exact: true })).toHaveCount(0)
  await expect(page.getByRole('button', { name: 'Files root' })).toBeEnabled()

  await page.getByRole('button', { name: 'Files root' }).click()
  page.once('dialog', (dialog) => dialog.accept('renamed.wav'))
  await page.getByText('tone.wav', { exact: true }).locator('..').getByRole('button', { name: 'Rename' }).click()
  await expect(page.getByText('renamed.wav', { exact: true })).toBeVisible()
  const download = page.waitForEvent('download')
  await page.getByText('renamed.wav', { exact: true }).locator('..').getByRole('button', { name: 'Download' }).click()
  expect((await download).suggestedFilename()).toBe('renamed.wav')

  await page.locator('.files-panel').evaluate((element) => {
    const transfer = new DataTransfer()
    transfer.items.add(new File([new Uint8Array([4, 5])], 'dropped.dat'))
    element.dispatchEvent(new DragEvent('drop', { bubbles: true, dataTransfer: transfer }))
  })
  await expect(page.getByText('dropped.dat', { exact: true })).toBeVisible()

  const exported = page.waitForEvent('download')
  await page.getByRole('button', { name: 'Export ZIP' }).click()
  expect((await exported).suggestedFilename()).toBe('nullperator-backup.zip')

  const restoreZip = zipSync({ 'renamed.wav': strToU8('replacement'), 'restored/marker.dat': strToU8('marker') })
  await page.getByRole('button', { name: 'Restore ZIP' }).click()
  await page.locator('input[accept*="zip"]').setInputFiles({ name: 'restore.zip', mimeType: 'application/zip', buffer: Buffer.from(restoreZip) })
  await expect(page.getByText(/Restore preview.*1 conflicts/)).toBeVisible()
  await page.getByRole('button', { name: 'Keep both' }).click()
  await expect(page.getByText('renamed (2).wav', { exact: true })).toBeVisible()

  // The parser rejects an untrusted archive before the Files handle mutates
  // IDBFS; this is intentionally exercised through the panel, not a raw FS.
  await page.getByRole('button', { name: 'Restore ZIP' }).click()
  await page.locator('input[accept*="zip"]').setInputFiles({ name: 'unsafe.zip', mimeType: 'application/zip', buffer: Buffer.from(zipSync({ '../escape.dat': strToU8('no') })) })
  await expect(page.getByText(/Unsafe ZIP entry/)).toBeVisible()
  await expect.poll(() => page.evaluate(() => globalThis.__picoTrackerStorageTest.read('/data/renamed (2).wav'))).toEqual(Array.from(strToU8('replacement')))

  const ready = page.locator('[data-runtime-state="ready"]')
  await restartWorkbench(page)
  await ready.waitFor({ state: 'hidden', timeout: 10_000 })
  await expect(ready).toBeVisible({ timeout: 20_000 })
  await expect.poll(() => page.evaluate(() => globalThis.__picoTrackerStorageTest.read('/data/renamed (2).wav'))).toEqual(Array.from(strToU8('replacement')))
  await expect.poll(() => page.evaluate(() => globalThis.__picoTrackerStorageTest.read('/data/dropped.dat'))).toEqual([4, 5])
  await stopWorkbench(page)
  await expect(page.locator('[data-runtime-state="idle"]')).toBeVisible({ timeout: 10_000 })
})
