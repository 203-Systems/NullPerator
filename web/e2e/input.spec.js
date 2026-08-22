import { expect, test } from '@playwright/test'

const virtualActions = [
  ['Left', 0],
  ['Down', 1],
  ['Right', 2],
  ['Up', 3],
  ['ALT', 4],
  ['EDIT', 5],
  ['ENTER', 6],
  ['NAV', 7],
  ['PLAY', 8],
  ['SELECT', 9],
  ['POWER', 10],
]

function actionMask(canvas) {
  return canvas.getAttribute('data-action-mask')
}

function actionGeneration(canvas) {
  return canvas.getAttribute('data-action-generation')
}

test('every virtual control reaches its specific C++ UI action path', async ({ page }) => {
  await page.goto('/?inputDiagnostics=1')
  await expect(page.locator('[data-runtime-state="ready"]')).toBeVisible()
  const canvas = page.locator('#picotracker-canvas')

  for (const [label, action] of virtualActions) {
    const button = page.getByRole('button', { name: label, exact: true })
    const pointerId = action + 100
    await button.dispatchEvent('pointerdown', { pointerId, pointerType: 'touch' })
    await expect.poll(() => actionMask(canvas)).toBe(String(1 << action))
    await button.dispatchEvent('pointerup', { pointerId, pointerType: 'touch' })
    await expect.poll(() => actionMask(canvas)).toBe('0')
  }
})

test('blur and pointer cancellation clear C++ state and stop further input dispatch', async ({ page }) => {
  await page.goto('/?inputDiagnostics=1')
  await expect(page.locator('[data-runtime-state="ready"]')).toBeVisible()
  const canvas = page.locator('#picotracker-canvas')

  await canvas.focus()
  await page.keyboard.down('ArrowDown')
  await expect.poll(() => actionMask(canvas)).toBe(String(1 << 1))
  await page.evaluate(() => window.dispatchEvent(new Event('blur')))
  await page.keyboard.up('ArrowDown')
  await expect.poll(() => actionMask(canvas)).toBe('0')
  const blurGeneration = await actionGeneration(canvas)
  await page.waitForTimeout(350)
  await expect(actionGeneration(canvas)).resolves.toBe(blurGeneration)

  const enter = page.getByRole('button', { name: 'ENTER', exact: true })
  await enter.dispatchEvent('pointerdown', { pointerId: 200, pointerType: 'touch' })
  await expect.poll(() => actionMask(canvas)).toBe(String(1 << 6))
  await enter.dispatchEvent('pointercancel', { pointerId: 200, pointerType: 'touch' })
  await expect.poll(() => actionMask(canvas)).toBe('0')
  const cancelGeneration = await actionGeneration(canvas)
  await page.waitForTimeout(350)
  await expect(actionGeneration(canvas)).resolves.toBe(cancelGeneration)

  // pointercancel must not leave the button's pointer-click suppression armed:
  // a subsequent real keyboard activation still travels through InputMap.
  await enter.focus()
  await page.keyboard.press('Space')
  await expect.poll(() => actionGeneration(canvas)).toBe(String(Number(cancelGeneration) + 2))
  await expect(canvas).toHaveAttribute('data-last-action', '6')
  await expect(canvas).toHaveAttribute('data-action-mask', '0')
})

test('focused virtual buttons activate with keyboard click semantics without global tracker keys', async ({ page }) => {
  await page.goto('/?inputDiagnostics=1')
  await expect(page.locator('[data-runtime-state="ready"]')).toBeVisible()
  const canvas = page.locator('#picotracker-canvas')
  const enter = page.getByRole('button', { name: 'ENTER', exact: true })

  await enter.focus()
  for (const key of ['Enter', 'Space']) {
    const generation = Number(await actionGeneration(canvas))
    await page.keyboard.press(key)
    await expect.poll(() => actionGeneration(canvas)).toBe(String(generation + 2))
    await expect(canvas).toHaveAttribute('data-last-action', '6')
    await expect(canvas).toHaveAttribute('data-action-mask', '0')
  }
})
