import { expect, test } from '@playwright/test'

const virtualActions = [
  ['Left', 0],
  ['Down', 1],
  ['Right', 2],
  ['Up', 3],
  ['ALT', 4],
  ['EDIT', 5],
  ['ENTER', 6],
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


  const start = page.getByRole('button', { name: 'PLAY', exact: true })
  const startGeneration = Number(await actionGeneration(canvas))
  await start.dispatchEvent('pointerdown', { pointerId: 180, pointerType: 'touch' })
  await expect.poll(() => actionMask(canvas)).toBe(String(1 << 7))
  await start.dispatchEvent('pointerup', { pointerId: 180, pointerType: 'touch' })
  await expect.poll(() => actionGeneration(canvas)).toBe(String(startGeneration + 4))
  await expect(canvas).toHaveAttribute('data-last-action', '8')
  await expect(canvas).toHaveAttribute('data-action-mask', '0')
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

test('Operator fixed WASD, JK, and XC controls reach C++ and preserve Node START semantics', async ({ page }) => {
  await page.goto('/?inputDiagnostics=1')
  await expect(page.locator('[data-runtime-state="ready"]')).toBeVisible()
  const canvas = page.locator('#picotracker-canvas')
  // Device input remains active after using the workbench chrome. This mirrors
  // the common case where the user clicks the Device rail and starts playing.
  await page.getByRole('button', { name: 'Device', exact: true }).click()

  await expect(page.getByRole('button', { name: 'NAV', exact: true })).toHaveCount(0)
  await expect(page.getByRole('button', { name: 'SELECT', exact: true })).toHaveCount(0)
  await expect(page.getByRole('button', { name: 'POWER', exact: true })).toHaveCount(0)

  for (const [key, action] of [['w', 3], ['a', 0], ['s', 1], ['d', 2], ['j', 6], ['k', 5]]) {
    await page.keyboard.down(key)
    await expect.poll(() => actionMask(canvas)).toBe(String(1 << action))
    await page.keyboard.up(key)
    await expect.poll(() => actionMask(canvas)).toBe('0')
  }


  const tapGeneration = Number(await actionGeneration(canvas))
  await page.keyboard.down('c')
  await expect.poll(() => actionMask(canvas)).toBe(String(1 << 7))
  await page.keyboard.up('c')
  await expect.poll(() => actionGeneration(canvas)).toBe(String(tapGeneration + 4))
  await expect(canvas).toHaveAttribute('data-last-action', '8')
  await expect(canvas).toHaveAttribute('data-action-mask', '0')

  const holdGeneration = Number(await actionGeneration(canvas))
  await page.keyboard.down('c')
  await expect.poll(() => actionMask(canvas)).toBe(String(1 << 7))
  await page.waitForTimeout(550)
  await page.keyboard.up('c')
  await expect.poll(() => actionGeneration(canvas)).toBe(String(holdGeneration + 2))
  await expect(canvas).toHaveAttribute('data-last-action', '7')
  await expect(canvas).toHaveAttribute('data-action-mask', '0')

  await page.keyboard.down('x')
  await expect.poll(() => actionMask(canvas)).toBe(String(1 << 4))
  await page.keyboard.down('j')
  await expect.poll(() => actionMask(canvas)).toBe(String((1 << 4) | (1 << 6)))
  await page.keyboard.up('j')
  await expect.poll(() => actionMask(canvas)).toBe(String(1 << 4))
  await page.keyboard.up('x')
  await expect.poll(() => actionMask(canvas)).toBe('0')

  const altPlayGeneration = Number(await actionGeneration(canvas))
  await page.keyboard.down('x')
  await expect.poll(() => actionMask(canvas)).toBe(String(1 << 4))
  await page.keyboard.down('c')
  await expect.poll(() => actionMask(canvas)).toBe(String((1 << 4) | (1 << 8)))
  await page.keyboard.up('c')
  await expect.poll(() => actionGeneration(canvas)).toBe(String(altPlayGeneration + 3))
  await expect(canvas).toHaveAttribute('data-last-action', '8')
  await expect(canvas).toHaveAttribute('data-action-mask', String(1 << 4))
  await page.keyboard.up('x')
  await expect.poll(() => actionMask(canvas)).toBe('0')

  await page.keyboard.down('x')
  await page.keyboard.down('c')
  await expect.poll(() => actionMask(canvas)).toBe(String((1 << 4) | (1 << 8)))
  await page.keyboard.down('k')
  await expect.poll(() => actionMask(canvas)).toBe(String((1 << 4) | (1 << 5)))
  await page.keyboard.up('k')
  await expect.poll(() => actionMask(canvas)).toBe(String(1 << 4))
  await page.keyboard.up('c')
  await expect.poll(() => actionMask(canvas)).toBe(String(1 << 4))
  await page.keyboard.up('x')
  await expect.poll(() => actionMask(canvas)).toBe('0')

  await page.keyboard.down('c')
  await expect.poll(() => actionMask(canvas)).toBe(String(1 << 7))
  await page.keyboard.down('x')
  await expect.poll(() => actionMask(canvas)).toBe(String((1 << 4) | (1 << 7)))
  await page.keyboard.up('x')
  await expect.poll(() => actionMask(canvas)).toBe(String(1 << 7))
  await page.keyboard.up('c')
  await expect.poll(() => actionMask(canvas)).toBe('0')
})
