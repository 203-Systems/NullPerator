import { expect, test } from '@playwright/test'

const mobileViewports = [
  { name: 'compact portrait', width: 320, height: 480 },
  { name: 'portrait', width: 320, height: 568 },
  { name: 'narrow landscape', width: 480, height: 320 },
  { name: 'landscape', width: 568, height: 320 },
  { name: 'modern phone landscape', width: 844, height: 390 },
  { name: 'large phone landscape', width: 932, height: 430 },
]

for (const viewport of mobileViewports) {
  test(`play mode stays usable at the ${viewport.name} mobile viewport`, async ({ page }) => {
    await page.setViewportSize(viewport)
    await page.goto('/?audio=disabled')

    const dashboard = page.locator('.dashboard')
    const canvas = page.locator('#picotracker-canvas')
    const settings = page.getByRole('button', { name: 'Settings', exact: true })
    const controls = page.locator('[data-action]')

    await expect(dashboard).toHaveAttribute('data-developer-mode', 'false')
    await expect(canvas).toHaveAttribute('data-frame-content', 'rendered', { timeout: 20_000 })
    await expect(controls).toHaveCount(8)
    await expect(settings).toHaveCount(1)
    const settingsBox = await settings.boundingBox()
    expect(settingsBox).not.toBeNull()
    expect(settingsBox.width).toBeGreaterThanOrEqual(44)
    expect(settingsBox.height).toBeGreaterThanOrEqual(44)

    const canvasBox = await canvas.boundingBox()
    expect(canvasBox).not.toBeNull()
    expect(canvasBox.width).toBe(240)
    expect(canvasBox.height).toBe(240)
    expect(canvasBox.x).toBeGreaterThanOrEqual(0)
    expect(canvasBox.y).toBeGreaterThanOrEqual(0)
    expect(canvasBox.x + canvasBox.width).toBeLessThanOrEqual(viewport.width)
    expect(canvasBox.y + canvasBox.height).toBeLessThanOrEqual(viewport.height)

    const pageGeometry = await page.evaluate(() => ({
      document: {
        clientWidth: document.documentElement.clientWidth,
        clientHeight: document.documentElement.clientHeight,
        scrollWidth: document.documentElement.scrollWidth,
        scrollHeight: document.documentElement.scrollHeight,
      },
      body: {
        clientWidth: document.body.clientWidth,
        clientHeight: document.body.clientHeight,
        scrollWidth: document.body.scrollWidth,
        scrollHeight: document.body.scrollHeight,
      },
    }))
    expect(pageGeometry).toEqual({
      document: {
        clientWidth: viewport.width,
        clientHeight: viewport.height,
        scrollWidth: viewport.width,
        scrollHeight: viewport.height,
      },
      body: {
        clientWidth: viewport.width,
        clientHeight: viewport.height,
        scrollWidth: viewport.width,
        scrollHeight: viewport.height,
      },
    })

    for (const control of await controls.all()) {
      const box = await control.boundingBox()
      expect(box).not.toBeNull()
      expect(box.width).toBeGreaterThanOrEqual(44)
      expect(box.height).toBeGreaterThanOrEqual(44)
      expect(box.x).toBeGreaterThanOrEqual(0)
      expect(box.y).toBeGreaterThanOrEqual(0)
      expect(box.x + box.width).toBeLessThanOrEqual(viewport.width)
      expect(box.y + box.height).toBeLessThanOrEqual(viewport.height)
    }

    const down = page.getByRole('button', { name: 'Down', exact: true })
    await down.dispatchEvent('pointerdown', { pointerId: 901, pointerType: 'touch' })
    await expect(down).toHaveAttribute('aria-pressed', 'true')
    await page.waitForTimeout(650)
    await expect(down).toHaveAttribute('aria-pressed', 'true')
    await down.dispatchEvent('pointerup', { pointerId: 901, pointerType: 'touch' })
    await expect(down).toHaveAttribute('aria-pressed', 'false')

    await settings.click()
    const dialog = page.getByRole('dialog', { name: 'Settings' })
    await expect(dialog).toBeVisible()
    const closeBox = await dialog.getByRole('button', { name: 'Close settings' }).boundingBox()
    expect(closeBox).not.toBeNull()
    expect(closeBox.width).toBeGreaterThanOrEqual(44)
    expect(closeBox.height).toBeGreaterThanOrEqual(44)
    await expect(dialog.getByRole('button', { name: 'Developer mode' })).toHaveCount(1)
    await dialog.getByRole('button', { name: 'Developer mode' }).click()

    await expect(dashboard).toHaveAttribute('data-developer-mode', 'true')
    await expect(page.getByRole('button', { name: 'Developer mode' })).toBeFocused()
    const navigation = page.getByRole('navigation', { name: 'Workbench sections' })
    await expect(navigation).toBeVisible()
    const developerToggle = page.getByRole('button', { name: 'Developer mode' })
    const developerToggleBox = await developerToggle.boundingBox()
    expect(developerToggleBox).not.toBeNull()
    expect(developerToggleBox.height).toBeGreaterThanOrEqual(44)
    const developerSettings = page.getByRole('button', { name: 'Settings', exact: true })
    await expect(developerSettings).toHaveCount(1)
    if (viewport.height < 400) {
      expect(await navigation.evaluate((element) => getComputedStyle(element).overflowY)).toBe('auto')
      const about = navigation.getByRole('button', { name: 'About', exact: true })
      await about.scrollIntoViewIfNeeded()
      await expect(about).toBeInViewport()
      await about.click()
      await expect(page.getByRole('heading', { name: 'About', exact: true })).toBeVisible()
    }
    await developerSettings.click()
    await expect(page.getByRole('heading', { name: 'Settings', exact: true })).toBeVisible()

    await page.getByRole('button', { name: 'Developer mode' }).click()
    await expect(dashboard).toHaveAttribute('data-developer-mode', 'false')
    await expect(page.getByRole('button', { name: 'Settings', exact: true })).toBeFocused()
    await expect.poll(() => page.locator('.operator-device').evaluate(
      (element) => element.style.getPropertyValue('--device-scale'),
    )).toBe('1')
    await expect.poll(() => page.locator('.device-scene').evaluate(
      (element) => ({ left: element.scrollLeft, top: element.scrollTop }),
    )).toEqual({ left: 0, top: 0 })
    const compactCanvasBox = await canvas.boundingBox()
    expect(compactCanvasBox).not.toBeNull()
    expect(compactCanvasBox.x).toBeGreaterThanOrEqual(0)
    expect(compactCanvasBox.y).toBeGreaterThanOrEqual(0)
    expect(compactCanvasBox.x + compactCanvasBox.width).toBeLessThanOrEqual(viewport.width)
    expect(compactCanvasBox.y + compactCanvasBox.height).toBeLessThanOrEqual(viewport.height)
  })
}

test('play settings is a global native modal and restores its trigger', async ({ page }) => {
  await page.setViewportSize({ width: 390, height: 844 })
  await page.goto('/?audio=disabled')

  const settings = page.getByRole('button', { name: 'Settings', exact: true })
  await settings.click()

  const dialog = page.getByRole('dialog', { name: 'Settings' })
  const close = dialog.getByRole('button', { name: 'Close settings' })
  const developer = dialog.getByRole('button', { name: 'Developer mode' })
  await expect(dialog).toBeVisible()
  await expect(page.locator('.settings-sheet:modal')).toHaveCount(1)
  await expect(close).toBeFocused()

  // Native modal inertness must reject focus attempts from the background.
  await settings.evaluate((element) => element.focus())
  await expect(close).toBeFocused()

  await page.keyboard.press('Shift+Tab')
  await expect(developer).toBeFocused()
  await page.keyboard.press('Tab')
  await expect(close).toBeFocused()

  await page.mouse.click(2, 2)
  await expect(dialog).toHaveCount(0)
  await expect(settings).toBeFocused()

  await settings.click()
  await page.keyboard.press('Escape')
  await expect(dialog).toHaveCount(0)
  await expect(settings).toBeFocused()
})

test('automatic mode follows live viewport changes and clears a departing settings sheet', async ({ page }) => {
  await page.setViewportSize({ width: 1024, height: 768 })
  await page.goto('/?audio=disabled')

  const dashboard = page.locator('.dashboard')
  const workspace = page.locator('.workspace')
  await expect(dashboard).toHaveAttribute('data-developer-mode', 'true')
  await page.getByRole('button', { name: 'Settings', exact: true }).focus()

  await page.setViewportSize({ width: 320, height: 568 })
  await expect(dashboard).toHaveAttribute('data-developer-mode', 'false')
  await expect(page.getByRole('button', { name: 'Settings', exact: true })).toBeFocused()

  await page.getByRole('button', { name: 'Settings', exact: true }).click()
  await expect(page.getByRole('dialog', { name: 'Settings' })).toBeVisible()
  await expect(workspace).toHaveAttribute('inert', '')

  await page.setViewportSize({ width: 1024, height: 768 })
  await expect(dashboard).toHaveAttribute('data-developer-mode', 'true')
  await expect(page.getByRole('button', { name: 'Developer mode' })).toBeFocused()
  await expect(page.getByRole('dialog', { name: 'Settings' })).toHaveCount(0)
  await expect(workspace).not.toHaveAttribute('inert', '')
})

test('forced developer mode preserves an explicit disabled preference', async ({ page }) => {
  await page.setViewportSize({ width: 320, height: 568 })
  await page.goto('/?audio=disabled&dev=1')

  const dashboard = page.locator('.dashboard')
  const toggle = page.getByRole('button', { name: 'Developer mode' })
  await expect(dashboard).toHaveAttribute('data-developer-mode', 'true')
  await toggle.click()
  await expect(dashboard).toHaveAttribute('data-developer-mode', 'true')
  await expect(toggle).toBeFocused()
  await expect.poll(() => page.evaluate(() => JSON.parse(
    localStorage.getItem('picotracker.wasm.settings.v4'),
  ).developerMode)).toBe(false)

  await page.goto('/?audio=disabled')
  await expect(dashboard).toHaveAttribute('data-developer-mode', 'false')
})

test('short-screen runtime recovery remains fully visible and restarts in place', async ({ page }) => {
  await page.setViewportSize({ width: 320, height: 480 })
  await page.goto('/?audio=disabled&runtime-fail-test=1')

  const workspace = page.locator('.workspace')
  const recovery = page.locator('[data-recovery-kind="runtime"]')
  const retry = page.getByRole('button', { name: 'Retry runtime' })
  const simulator = page.locator('.device-stage')
  const canvas = page.locator('#picotracker-canvas')
  await expect(recovery).toBeVisible({ timeout: 20_000 })
  await expect(retry).toBeFocused()
  await expect(simulator).toHaveAttribute('inert', '')
  await expect(page.getByRole('region', { name: 'Operator simulator' })).toHaveCount(0)
  await canvas.evaluate((element) => element.focus())
  await expect(retry).toBeFocused()
  await expect.poll(() => workspace.evaluate((element) => element.scrollTop)).toBe(0)
  const workspaceBox = await workspace.boundingBox()
  const recoveryBox = await recovery.boundingBox()
  expect(recoveryBox.y).toBeGreaterThanOrEqual(workspaceBox.y)
  await expect(retry).toBeInViewport()

  const down = page.locator('[data-action="down"]')
  await page.keyboard.down('s')
  await expect(down).toHaveAttribute('aria-pressed', 'false')
  await page.keyboard.up('s')

  await retry.click()
  await expect(canvas).toHaveAttribute('data-frame-content', 'rendered', { timeout: 20_000 })
  await expect(simulator).not.toHaveAttribute('inert', '')
  await expect(canvas).toBeFocused()
  await expect.poll(() => workspace.evaluate((element) => element.scrollTop)).toBe(0)
})
