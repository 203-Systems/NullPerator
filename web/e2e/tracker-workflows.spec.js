import { expect, test } from '@playwright/test'

async function deleteIdbfsDatabase(page) {
  await page.goto('/oracle.html')
  await page.evaluate(async () => {
    await new Promise((resolveDelete, reject) => {
      const request = indexedDB.deleteDatabase('/data')
      request.onsuccess = () => resolveDelete()
      request.onerror = () => reject(request.error)
      request.onblocked = () => reject(new Error('IDBFS database remained open during tracker-workflow cleanup'))
    })
  })
}

async function modelSnapshot(page) {
  return page.evaluate(() => globalThis.__picoTrackerViewsTest.modelSnapshot())
}

async function inputGeneration(page) {
  return Number(await page.locator('#picotracker-canvas').getAttribute('data-action-generation'))
}

async function tap(page, key) {
  const before = await inputGeneration(page)
  await page.keyboard.down(key)
  await expect.poll(() => inputGeneration(page)).toBeGreaterThanOrEqual(before + 1)
  await page.keyboard.up(key)
  await expect.poll(() => inputGeneration(page)).toBeGreaterThanOrEqual(before + 2)
}

async function chord(page, modifier, key) {
  const before = await inputGeneration(page)
  await page.keyboard.down(modifier)
  await expect.poll(() => inputGeneration(page)).toBeGreaterThanOrEqual(before + 1)
  await page.keyboard.down(key)
  await expect.poll(() => inputGeneration(page)).toBeGreaterThanOrEqual(before + 2)
  await page.keyboard.up(key)
  await expect.poll(() => inputGeneration(page)).toBeGreaterThanOrEqual(before + 3)
  await page.keyboard.up(modifier)
  await expect.poll(() => inputGeneration(page)).toBeGreaterThanOrEqual(before + 4)
}

test('real LIVE Left Play cues the current Song row on all eight tracks', async ({ page }) => {
  test.setTimeout(60_000)
  await deleteIdbfsDatabase(page)
  await page.goto('/?ui2=1&audio=disabled&storage-test=1&views-test=1&inputDiagnostics=1')
  await expect(page.locator('[data-runtime-state="ready"]')).toBeVisible({ timeout: 20_000 })
  await expect(page.locator('[data-storage-state="ready"]')).toBeVisible()

  // Build one playable Chain through the real Song -> Chain workflow. Every
  // Song column then references that same playable Chain on row 00.
  await tap(page, 'k')
  await chord(page, 'x', 'd')
  await tap(page, 'k')
  await chord(page, 'x', 'a')
  for (let track = 1; track < 8; track += 1) {
    await tap(page, 'd')
    await tap(page, 'k')
  }

  await expect.poll(() => modelSnapshot(page)).toMatchObject({
    playerRunning: false,
    playingTrackMask: 0,
  })

  // OPTION+LEFT is the approved PicoTracker Song/LIVE selector. Keep LEFT's
  // existing press-edge cursor move, then press PLAY while LEFT remains held.
  await chord(page, 'j', 'a')
  await chord(page, 'a', 'c')

  // A semantic transport mask proves that all eight native Player channels
  // started. This is independent of framebuffer color and screenshot timing.
  await expect.poll(() => modelSnapshot(page), { timeout: 10_000 }).toMatchObject({
    playerRunning: true,
    playingTrackMask: 0xFF,
  })
})
