import { expect, test } from '@playwright/test'

test('@visual UI2 exclusively owns the browser frame during incremental cursor updates', async ({ page }) => {
  await page.goto('/?ui2=1&audio=disabled')
  await expect(page.locator('[data-runtime-state="ready"]')).toBeVisible()

  const canvas = page.locator('#picotracker-canvas')
  await expect(canvas).toHaveAttribute('data-frame-content', 'rendered')

  const right = page.getByRole('button', { name: 'Right', exact: true })
  const down = page.getByRole('button', { name: 'Down', exact: true })
  await right.click()
  await down.click()
  await right.click()
  await down.click()

  // Wait past the cursor's non-linear settle so the golden represents the
  // stable T3 / row 02 frame. Any legacy framebuffer bleed changes this image.
  await page.waitForTimeout(500)
  await expect(canvas).toHaveScreenshot('ui2-song-cursor-t3-row-02.png')
})

test('@visual Chain runs through the native UI2 path with stable row and column motion', async ({ page }) => {
  await page.goto('/?ui2=1&audio=disabled&views-test=1&view=chain')
  await expect(page.locator('[data-runtime-state="ready"]')).toBeVisible()

  const canvas = page.locator('#picotracker-canvas')
  await expect(canvas).toHaveAttribute('data-frame-content', 'rendered')
  await page.getByRole('button', { name: 'Down', exact: true }).click()
  await page.getByRole('button', { name: 'Down', exact: true }).click()
  await page.getByRole('button', { name: 'Right', exact: true }).click()

  await page.waitForTimeout(500)
  await expect(canvas).toHaveScreenshot('ui2-chain-tr-row-02.png')
})
