import { expect, test } from '@playwright/test'

const fixturePath = '/data/idbfs-e2e-synthetic.wav'
const markerPath = '/data/idbfs-e2e-project-marker.dat'
const markerBytes = [0x50, 0x54, 0x38]
const fixtureBytes = new Uint8Array([
  0x52, 0x49, 0x46, 0x46, 0x24, 0x00, 0x00, 0x00,
  0x57, 0x41, 0x56, 0x45, 0x66, 0x6d, 0x74, 0x20,
  0x10, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00,
  0x44, 0xac, 0x00, 0x00, 0x88, 0x58, 0x01, 0x00,
  0x02, 0x00, 0x10, 0x00, 0x64, 0x61, 0x74, 0x61,
  0x00, 0x00, 0x00, 0x00,
])

async function deleteIdbfsDatabase(page) {
  await page.evaluate(async () => {
    await new Promise((resolve, reject) => {
      const request = indexedDB.deleteDatabase('/data')
      request.onsuccess = () => resolve()
      request.onerror = () => reject(request.error)
      request.onblocked = () => reject(new Error('IDBFS database remained open during test cleanup'))
    })
  })
}

async function readFixture(page, path = fixturePath) {
  return page.evaluate((path) => {
    return globalThis.__picoTrackerStorageTest.read(path)
  }, path)
}

test('IDBFS population, shutdown flush, restart, and reload retain a synthetic WAV and project', async ({ page }) => {
  test.setTimeout(75_000)
  await page.goto('/oracle.html')
  await deleteIdbfsDatabase(page)

  await page.goto('/?storage-test=1')
  await expect(page.locator('[data-runtime-state="ready"]')).toBeVisible({ timeout: 20_000 })
  await page.evaluate(async ({ path, bytes }) => {
    const storage = globalThis.__picoTrackerStorageTest
    storage.write(path, bytes)
    storage.write('/data/idbfs-e2e-project-marker.dat', [0x50, 0x54, 0x38])
    await storage.flush()
  }, { path: fixturePath, bytes: Array.from(fixtureBytes) })
  expect(await readFixture(page)).toEqual(Array.from(fixtureBytes))
  expect(await readFixture(page, markerPath)).toEqual(markerBytes)

  const ready = page.locator('[data-runtime-state="ready"]')
  await page.getByRole('button', { name: 'Restart' }).click()
  await ready.waitFor({ state: 'hidden', timeout: 10_000 })
  await expect(ready).toBeVisible({ timeout: 20_000 })
  expect(await readFixture(page)).toEqual(Array.from(fixtureBytes))
  expect(await readFixture(page, markerPath)).toEqual(markerBytes)

  await page.reload()
  await expect(page.locator('[data-runtime-state="ready"]')).toBeVisible({ timeout: 20_000 })
  expect(await readFixture(page)).toEqual(Array.from(fixtureBytes))
  expect(await readFixture(page, markerPath)).toEqual(markerBytes)

  await page.getByRole('button', { name: 'Stop runtime' }).click()
  await expect(page.locator('[data-runtime-state="idle"]')).toBeVisible({ timeout: 10_000 })
})
