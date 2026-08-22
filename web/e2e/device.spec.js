import { expect, test } from '@playwright/test'

test('boots the C++ UI at 240x240 logical pixels', async ({ page }) => {
  page.on('console', (message) => console.log(`[browser:${message.type()}] ${message.text()}`))
  page.on('pageerror', (error) => console.log(`[browser:error] ${error.message}`))
  await page.goto('/')
  await expect(page.locator('[data-runtime-state="ready"]')).toBeVisible()
  await expect(page.locator('#picotracker-canvas')).toHaveAttribute('width', '240')
  await expect(page.locator('#picotracker-canvas')).toHaveAttribute('height', '240')
  await expect(page.locator('#picotracker-canvas')).toHaveAttribute('data-frame-content', 'rendered')
  await page.waitForTimeout(250)
  await expect(page.locator('#picotracker-canvas')).toHaveScreenshot('device-boot.png')
})

test('stops the application thread before restarting the tracker canvas', async ({ page }) => {
  await page.goto('/')
  await expect(page.locator('[data-runtime-state="ready"]')).toBeVisible()

  await page.getByRole('button', { name: 'Restart' }).click()

  await expect(page.locator('[data-runtime-state="ready"]')).toBeVisible({ timeout: 15_000 })
  await expect(page.locator('#picotracker-canvas')).toHaveAttribute('data-frame-content', 'rendered')
})
