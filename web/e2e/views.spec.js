import { expect, test } from '@playwright/test'
import { stopWorkbench } from './helpers/runtime.js'

async function captureCanvasHeader(page) {
  const box = await page.locator('#picotracker-canvas').boundingBox()
  if (box == null) throw new Error('UI2 canvas has no visible bounds')
  const scaleX = box.width / 240
  const scaleY = box.height / 240
  return page.screenshot({
    clip: {
      x: box.x + (4 * scaleX),
      y: box.y + (4 * scaleY),
      width: 180 * scaleX,
      height: 24 * scaleY,
    },
  })
}

test('every registered C++ view and modal enters, draws, and processes input on the application pthread', async ({ page }) => {
  test.setTimeout(90_000)
  await page.goto('/?views-test=1')
  await expect(page.locator('[data-runtime-state="ready"]')).toBeVisible({ timeout: 20_000 })
  const names = await page.evaluate(() => globalThis.__picoTrackerViewsTest.names)
  expect(names).toHaveLength(20)
  expect(names.at(-1)).toBe('Font')
  const subtypeHeaders = new Map()
  const subtypeViews = new Set([6, 7, 10, 11, 12, 14, 15])

  for (let viewType = 0; viewType < names.length; viewType += 1) {
    const beforeDraw = await page.evaluate(() => globalThis.__picoTrackerViewsTest.generation())
    await page.evaluate((requested) => globalThis.__picoTrackerViewsTest.request(requested), viewType)
    await expect.poll(() => page.evaluate(() => ({
      current: globalThis.__picoTrackerViewsTest.current(),
      generation: globalThis.__picoTrackerViewsTest.generation(),
    })), { message: `${names[viewType]} did not finish its C++ draw path` }).toEqual({
      current: viewType,
      generation: beforeDraw + 1,
    })
    if (subtypeViews.has(viewType)) {
      subtypeHeaders.set(viewType, await captureCanvasHeader(page))
    }

    const beforeInput = await page.evaluate(() => globalThis.__picoTrackerViewsTest.inputGeneration())
    const left = page.getByRole('button', { name: 'Left', exact: true })
    await left.dispatchEvent('pointerdown', { pointerId: 700 + viewType, pointerType: 'touch' })
    await left.dispatchEvent('pointerup', { pointerId: 700 + viewType, pointerType: 'touch' })
    await expect.poll(() => page.evaluate(() => globalThis.__picoTrackerViewsTest.inputGeneration()), {
      message: `${names[viewType]} did not return from C++ input processing`,
    }).toBe(beforeInput + 2)
    await expect.poll(() => page.evaluate(() => globalThis.__picoTrackerViewsTest.current())).toBe(viewType)
  }

  expect(subtypeHeaders.get(6).equals(subtypeHeaders.get(7)),
    'Phrase Table and Instrument Table must render distinct P## / I## headers').toBe(false)
  expect(subtypeHeaders.get(10).equals(subtypeHeaders.get(11)),
    'Sample and Instrument imports share the approved IMPORT chrome').toBe(true)
  const distinctBrowserHeaders = [10, 12, 14]
    .map((viewType) => subtypeHeaders.get(viewType).toString('base64'))
  expect(new Set(distinctBrowserHeaders).size,
    'Import, Project, and Theme diagnostics must keep distinct chrome').toBe(3)
  expect(subtypeHeaders.get(14).equals(subtypeHeaders.get(15)),
    'Select Theme and Theme Import share the real Theme browser controller').toBe(true)

  const modalNames = await page.evaluate(() => globalThis.__picoTrackerViewsTest.modalNames)
  expect(modalNames).toHaveLength(5)
  for (let modalType = 0; modalType < modalNames.length; modalType += 1) {
    const beforeDraw = await page.evaluate(() => globalThis.__picoTrackerViewsTest.modalGeneration())
    await page.evaluate((requested) => globalThis.__picoTrackerViewsTest.openModal(requested), modalType)
    await expect.poll(() => page.evaluate(() => ({
      current: globalThis.__picoTrackerViewsTest.currentModal(),
      generation: globalThis.__picoTrackerViewsTest.modalGeneration(),
    })), { message: `${modalNames[modalType]} did not finish its C++ draw path` }).toEqual({
      current: modalType,
      generation: beforeDraw + 1,
    })

    const beforeInput = await page.evaluate(() => globalThis.__picoTrackerViewsTest.inputGeneration())
    const left = page.getByRole('button', { name: 'Left', exact: true })
    await left.dispatchEvent('pointerdown', { pointerId: 800 + modalType, pointerType: 'touch' })
    await left.dispatchEvent('pointerup', { pointerId: 800 + modalType, pointerType: 'touch' })
    await expect.poll(() => page.evaluate(() => globalThis.__picoTrackerViewsTest.inputGeneration()), {
      message: `${modalNames[modalType]} did not return from C++ input processing`,
    }).toBe(beforeInput + 2)
    await expect.poll(() => page.evaluate(() => globalThis.__picoTrackerViewsTest.currentModal())).toBe(modalType)

    const beforeClose = await page.evaluate(() => globalThis.__picoTrackerViewsTest.modalGeneration())
    await page.evaluate(() => globalThis.__picoTrackerViewsTest.closeModal())
    await expect.poll(() => page.evaluate(() => ({
      current: globalThis.__picoTrackerViewsTest.currentModal(),
      generation: globalThis.__picoTrackerViewsTest.modalGeneration(),
    })), { message: `${modalNames[modalType]} did not close through the C++ modal path` }).toEqual({
      current: 0xFFFFFFFF,
      generation: beforeClose + 1,
    })
  }

  await expect(page.locator('#picotracker-canvas')).toHaveAttribute('data-frame-content', 'rendered')
  await stopWorkbench(page)
  await expect(page.locator('[data-runtime-state="idle"]')).toBeVisible({ timeout: 10_000 })
  await expect.poll(() => page.evaluate(() => typeof globalThis.__picoTrackerViewsTest)).toBe('undefined')
})
