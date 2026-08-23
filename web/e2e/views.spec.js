import { expect, test } from '@playwright/test'

test('every registered C++ view and modal enters, draws, and processes input on the application pthread', async ({ page }) => {
  test.setTimeout(90_000)
  await page.goto('/?views-test=1')
  await expect(page.locator('[data-runtime-state="ready"]')).toBeVisible({ timeout: 20_000 })
  const names = await page.evaluate(() => globalThis.__picoTrackerViewsTest.names)
  expect(names).toHaveLength(19)

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

    const beforeInput = await page.evaluate(() => globalThis.__picoTrackerViewsTest.inputGeneration())
    const left = page.getByRole('button', { name: 'Left', exact: true })
    await left.dispatchEvent('pointerdown', { pointerId: 700 + viewType, pointerType: 'touch' })
    await left.dispatchEvent('pointerup', { pointerId: 700 + viewType, pointerType: 'touch' })
    await expect.poll(() => page.evaluate(() => globalThis.__picoTrackerViewsTest.inputGeneration()), {
      message: `${names[viewType]} did not return from C++ input processing`,
    }).toBe(beforeInput + 2)
    await expect.poll(() => page.evaluate(() => globalThis.__picoTrackerViewsTest.current())).toBe(viewType)
  }

  const modalNames = await page.evaluate(() => globalThis.__picoTrackerViewsTest.modalNames)
  expect(modalNames).toHaveLength(4)
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
  await page.getByRole('button', { name: 'Stop runtime' }).click()
  await expect(page.locator('[data-runtime-state="idle"]')).toBeVisible({ timeout: 10_000 })
  await expect.poll(() => page.evaluate(() => typeof globalThis.__picoTrackerViewsTest)).toBe('undefined')
})
