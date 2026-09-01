import { expect, test } from '@playwright/test'

async function deleteIdbfsDatabase(page) {
  await page.goto('/oracle.html')
  await page.evaluate(async () => {
    await new Promise((resolveDelete, reject) => {
      const request = indexedDB.deleteDatabase('/data')
      request.onsuccess = () => resolveDelete()
      request.onerror = () => reject(request.error)
      request.onblocked = () => reject(new Error('IDBFS database remained open during project-lifecycle cleanup'))
    })
  })
}

async function modelSnapshot(page) {
  return page.evaluate(() => globalThis.__picoTrackerViewsTest.modelSnapshot())
}

async function expectModel(page, expected) {
  await expect.poll(() => modelSnapshot(page), { timeout: 10_000 }).toMatchObject(expected)
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

async function projectExists(page, projectName) {
  return page.evaluate(
    (name) => globalThis.__picoTrackerStorageTest.exists(`/data/projects/${name}/lgptsav.dat`),
    projectName,
  )
}

test('real session Save As, Browser Load/Delete, dirty confirmation, and New restore model data', async ({ page }) => {
  test.setTimeout(120_000)
  await deleteIdbfsDatabase(page)

  await page.goto('/?ui2=1&audio=disabled&storage-test=1&views-test=1&inputDiagnostics=1')
  await expect(page.locator('[data-runtime-state="ready"]')).toBeVisible({ timeout: 20_000 })
  await expect(page.locator('[data-storage-state="ready"]')).toBeVisible()
  await expectModel(page, { projectName: '.untitled', tempo: 138, playerRunning: false })

  // Create project A with tempo 139. An empty rename cannot select SAVE, so
  // UP reaches RANDOM; the first ENTER generates a valid name and the second
  // accepts SAVE.
  await chord(page, 'x', 'w')
  await tap(page, 's')
  await tap(page, 'd')
  await tap(page, 'w')
  await tap(page, 'd')
  await tap(page, 'd')
  await tap(page, 'd')
  await tap(page, 'k')
  await tap(page, 'w')
  await tap(page, 'k')
  await tap(page, 'k')
  const projectA = (await modelSnapshot(page)).projectName
  expect(projectA).not.toBe('.untitled')

  await tap(page, 'a')
  await tap(page, 'k')
  await expect.poll(() => projectExists(page, projectA), { timeout: 10_000 }).toBe(true)

  // Create project B from A with a different tempo. Appending the selected
  // keyboard key makes the second project name deterministic: `${A}1`.
  await tap(page, 's')
  await tap(page, 'd')
  await tap(page, 'w')
  await tap(page, 'd')
  await tap(page, 'k')
  await tap(page, 's')
  await tap(page, 'k')
  await tap(page, 'w')
  await tap(page, 'w')
  await tap(page, 'k')
  const projectB = (await modelSnapshot(page)).projectName
  expect(projectB).toBe(`${projectA}1`)

  await tap(page, 'a')
  await tap(page, 'k')
  await expect.poll(() => projectExists(page, projectB), { timeout: 10_000 }).toBe(true)
  await expectModel(page, { projectName: projectB, tempo: 140 })

  // Load is forbidden while playback is active. The established OK message
  // consumes ENTER and leaves the selected project and live model untouched.
  await tap(page, 'c')
  await expectModel(page, { playerRunning: true })
  await tap(page, 'a')
  await tap(page, 'k')
  await tap(page, 'k')
  await expectModel(page, { projectName: projectB, tempo: 140, playerRunning: true })
  await tap(page, 'k')
  await tap(page, 'c')
  await expectModel(page, { playerRunning: false })
  // OPTION no longer doubles as Browser Back under the approved M8 mapping.
  // Keep global PLAY available in Project Browser, then use SHIFT+LEFT for the
  // documented return chord before editing Project values.
  await chord(page, 'x', 'a')

  // Make B dirty, reject the first load with the conservative default NO,
  // then explicitly select YES. Loading A must restore its persisted tempo,
  // proving this is a model restore rather than a project-name-only switch.
  await tap(page, 's')
  await tap(page, 'd')
  await tap(page, 'w')
  await tap(page, 'k')
  await tap(page, 'k')
  await tap(page, 'k')
  await expectModel(page, { projectName: projectB, tempo: 141 })
  await tap(page, 'k')
  await tap(page, 'a')
  await tap(page, 'k')
  await expectModel(page, { projectName: projectA, tempo: 139, playerRunning: false })

  // A successful load resets every page controller before entering Song.
  // Project therefore reopens on NEW. First delete non-current B through the
  // browser (again checking the default NO), then execute NEW from that reset.
  await chord(page, 'x', 'w')
  await tap(page, 'd')
  await tap(page, 'k')
  await tap(page, 's')
  await tap(page, 's')
  await tap(page, 'd')
  await tap(page, 'k')
  await tap(page, 'k')
  await expect.poll(() => projectExists(page, projectB)).toBe(true)
  await tap(page, 'k')
  await tap(page, 'a')
  await tap(page, 'k')
  await expect.poll(() => projectExists(page, projectB), { timeout: 10_000 }).toBe(false)

  await chord(page, 'x', 'a')
  await tap(page, 'a')
  await tap(page, 'k')
  await expectModel(page, { projectName: '.untitled', tempo: 138, playerRunning: false })
  await expect.poll(() => projectExists(page, projectA)).toBe(true)
})
