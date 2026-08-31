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

async function openFreshTracker(page) {
  await deleteIdbfsDatabase(page)
  await page.goto('/?ui2=1&audio=disabled&storage-test=1&views-test=1&inputDiagnostics=1')
  await expect(page.locator('[data-runtime-state="ready"]')).toBeVisible({ timeout: 20_000 })
  await expect(page.locator('[data-storage-state="ready"]')).toBeVisible()
}

async function seedPlayableChainOnEveryTrack(page) {
  // Create Chain 00 and Phrase 00 through the real editor, then reference that
  // Chain from every Song track on row 00.
  await tap(page, 'k')
  await chord(page, 'x', 'd')
  await tap(page, 'k')
  await chord(page, 'x', 'a')
  for (let track = 1; track < 8; track += 1) {
    await tap(page, 'd')
    await tap(page, 'k')
  }
}

test('real LIVE Left Play cues the current Song row on all eight tracks', async ({ page }) => {
  test.setTimeout(60_000)
  await openFreshTracker(page)
  await seedPlayableChainOnEveryTrack(page)

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

test('real Chain transport ignores the persistent LIVE Song selector', async ({ page }) => {
  test.setTimeout(60_000)
  await openFreshTracker(page)
  await seedPlayableChainOnEveryTrack(page)

  // The setup leaves track 7 selected. Enter the approved persistent LIVE
  // mode, then navigate to Chain without changing that selector.
  await chord(page, 'j', 'a')
  await chord(page, 'x', 'd')

  // Plain Chain PLAY is local context transport even while Song remains LIVE.
  // The real native Player must start only the selected track, then the next
  // plain PLAY must stop it again.
  await tap(page, 'c')
  await expect.poll(() => modelSnapshot(page), { timeout: 10_000 }).toMatchObject({
    playerRunning: true,
    playingTrackMask: 0x80,
  })

  await tap(page, 'c')
  await expect.poll(() => modelSnapshot(page), { timeout: 10_000 }).toMatchObject({
    playerRunning: false,
    playingTrackMask: 0,
  })

  // Returning to Song and tapping PLAY must still use LIVE cue semantics. If
  // Chain transport reset the selector to SONG, all eight tracks would start.
  await chord(page, 'x', 'a')
  await tap(page, 'c')
  await expect.poll(() => modelSnapshot(page), { timeout: 10_000 }).toMatchObject({
    playerRunning: true,
    playingTrackMask: 0x80,
  })
})
