import { expect, test } from '@playwright/test'

const mobileViewports = [
  { name: 'portrait', width: 320, height: 568 },
  { name: 'narrow landscape', width: 480, height: 320 },
  { name: 'landscape', width: 568, height: 320 },
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
    await expect(dialog.getByRole('button', { name: 'Developer mode' })).toHaveCount(1)
    await dialog.getByRole('button', { name: 'Developer mode' }).click()

    await expect(dashboard).toHaveAttribute('data-developer-mode', 'true')
    const navigation = page.getByRole('navigation', { name: 'Workbench sections' })
    await expect(navigation).toBeVisible()
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
    await expect(page.getByRole('button', { name: 'Settings', exact: true })).toHaveCount(1)
  })
}
