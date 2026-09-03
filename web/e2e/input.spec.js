import { expect, test } from '@playwright/test'

const virtualActions = [
  ['Left', 0],
  ['Down', 1],
  ['Right', 2],
  ['Up', 3],
  ['SHIFT', 4],
  ['OPTION', 5],
  ['ENTER', 6],
  ['PLAY', 7],
]

function actionMask(canvas) {
  return canvas.getAttribute('data-action-mask')
}

function actionGeneration(canvas) {
  return canvas.getAttribute('data-action-generation')
}

test('every virtual control reaches its specific C++ UI action path', async ({ page }) => {
  await page.goto('/?audio=disabled&inputDiagnostics=1')
  await expect(page.locator('[data-runtime-state="ready"]')).toBeVisible()
  const canvas = page.locator('#picotracker-canvas')

  for (const [label, action] of virtualActions) {
    const button = page.getByRole('button', { name: label, exact: true })
    const pointerId = action + 100
    const generation = Number(await actionGeneration(canvas))
    await button.dispatchEvent('pointerdown', { pointerId, pointerType: 'touch' })
    await expect.poll(() => actionMask(canvas)).toBe(String(1 << action))
    await button.dispatchEvent('pointerup', { pointerId, pointerType: 'touch' })
    await expect.poll(() => actionMask(canvas)).toBe('0')
    await expect.poll(() => actionGeneration(canvas)).toBe(String(generation + 2))
    await expect(canvas).toHaveAttribute('data-last-action', String(action))
  }
})

test('blur and pointer cancellation clear C++ state and stop further input dispatch', async ({ page }) => {
  await page.goto('/?audio=disabled&inputDiagnostics=1')
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
  await page.goto('/?audio=disabled&inputDiagnostics=1')
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

test('Operator fixed WASD, J/K, and X/C controls reach C++ with direct M8 semantics', async ({ page }) => {
  await page.goto('/?audio=disabled&inputDiagnostics=1')
  await expect(page.locator('[data-runtime-state="ready"]')).toBeVisible()
  const canvas = page.locator('#picotracker-canvas')
  // Device input remains active after using the workbench chrome. This mirrors
  // the common case where the user clicks the Device rail and starts playing.
  await page.getByRole('button', { name: 'Device', exact: true }).click()

  await expect(page.getByRole('button', { name: 'NAV', exact: true })).toHaveCount(0)
  await expect(page.getByRole('button', { name: 'SELECT', exact: true })).toHaveCount(0)
  await expect(page.getByRole('button', { name: 'POWER', exact: true })).toHaveCount(0)
  expect(await page.locator('.bottom-buttons [data-action]').evaluateAll(
    (buttons) => buttons.map((button) => button.dataset.action),
  )).toEqual(['play', 'shift'])
  await expect(page.getByRole('button', { name: 'PLAY', exact: true }).locator('kbd')).toHaveText('C')
  await expect(page.getByRole('button', { name: 'SHIFT', exact: true }).locator('kbd')).toHaveText('X')

  const keyActions = [
    ['w', 'up', 3], ['a', 'left', 0], ['s', 'down', 1], ['d', 'right', 2],
    ['j', 'option', 5], ['k', 'enter', 6], ['x', 'shift', 4], ['c', 'play', 7],
  ]
  for (const [key, name, action] of keyActions) {
    const control = page.locator(`[data-action="${name}"]`)
    await page.keyboard.down(key)
    await expect.poll(() => actionMask(canvas)).toBe(String(1 << action))
    await expect(control).toHaveAttribute('aria-pressed', 'true')
    await page.keyboard.up(key)
    await expect.poll(() => actionMask(canvas)).toBe('0')
    await expect(control).toHaveAttribute('aria-pressed', 'false')
  }
  const holdGeneration = Number(await actionGeneration(canvas))
  await page.keyboard.down('c')
  await expect.poll(() => actionMask(canvas)).toBe(String(1 << 7))
  await page.waitForTimeout(550)
  await expect(actionGeneration(canvas)).resolves.toBe(String(holdGeneration + 1))
  await page.keyboard.up('c')
  await expect.poll(() => actionGeneration(canvas)).toBe(String(holdGeneration + 2))
  await expect(canvas).toHaveAttribute('data-last-action', '7')
  await expect(canvas).toHaveAttribute('data-action-mask', '0')

  await page.keyboard.down('x')
  await expect.poll(() => actionMask(canvas)).toBe(String(1 << 4))
  await page.keyboard.down('k')
  await expect.poll(() => actionMask(canvas)).toBe(String((1 << 4) | (1 << 6)))
  await page.keyboard.up('k')
  await expect.poll(() => actionMask(canvas)).toBe(String(1 << 4))
  await page.keyboard.up('x')
  await expect.poll(() => actionMask(canvas)).toBe('0')

  const shiftPlayGeneration = Number(await actionGeneration(canvas))
  await page.keyboard.down('x')
  await expect.poll(() => actionMask(canvas)).toBe(String(1 << 4))
  await page.keyboard.down('c')
  await expect.poll(() => actionMask(canvas)).toBe(String((1 << 4) | (1 << 7)))
  await page.keyboard.up('c')
  await expect.poll(() => actionGeneration(canvas)).toBe(String(shiftPlayGeneration + 3))
  await expect(canvas).toHaveAttribute('data-last-action', '7')
  await expect(canvas).toHaveAttribute('data-action-mask', String(1 << 4))
  await page.keyboard.up('x')
  await expect.poll(() => actionMask(canvas)).toBe('0')

  await page.keyboard.down('x')
  await page.keyboard.down('c')
  await expect.poll(() => actionMask(canvas)).toBe(String((1 << 4) | (1 << 7)))
  await page.keyboard.down('j')
  await expect.poll(() => actionMask(canvas)).toBe(String((1 << 4) | (1 << 5) | (1 << 7)))
  await page.keyboard.up('j')
  await expect.poll(() => actionMask(canvas)).toBe(String((1 << 4) | (1 << 7)))
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

test('held WASD directions keep scrolling UI2 list pages', async ({ page }) => {
  const pageErrors = []
  page.on('pageerror', (error) => pageErrors.push(error.message))
  for (const view of ['project', 'device', 'instrument']) {
    await page.goto(`/?ui2=1&audio=disabled&views-test=1&view=${view}&inputDiagnostics=1`)
    await expect(page.locator('[data-runtime-state="ready"]')).toBeVisible()
    const canvas = page.locator('#picotracker-canvas')
    const initialGeneration = Number(await actionGeneration(canvas))
    const beforeHold = await canvas.screenshot()

    await page.keyboard.down('s')
    await expect.poll(() => actionMask(canvas)).toBe(String(1 << 1))
    await page.waitForTimeout(760)
    expect(pageErrors).toEqual([])
    await expect.poll(async () => Number(await actionGeneration(canvas))).toBeGreaterThanOrEqual(initialGeneration + 4)
    expect((await canvas.screenshot()).equals(beforeHold)).toBe(false)
    await page.keyboard.up('s')
    await expect.poll(() => actionMask(canvas)).toBe('0')
  }

  await page.goto('/?ui2=1&audio=disabled&views-test=1&view=instrument&inputDiagnostics=1')
  await expect(page.locator('[data-runtime-state="ready"]')).toBeVisible()
  const canvas = page.locator('#picotracker-canvas')
  const initialGeneration = Number(await actionGeneration(canvas))
  const down = page.getByRole('button', { name: 'Down', exact: true })
  await down.dispatchEvent('pointerdown', { pointerId: 901, pointerType: 'touch' })
  await page.waitForTimeout(760)
  await expect.poll(async () => Number(await actionGeneration(canvas))).toBeGreaterThanOrEqual(initialGeneration + 4)
  await down.dispatchEvent('pointerup', { pointerId: 901, pointerType: 'touch' })
  await expect.poll(() => actionMask(canvas)).toBe('0')
})
