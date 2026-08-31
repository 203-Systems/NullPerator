import { expect, test } from '@playwright/test'
import { restartWorkbench } from './helpers/runtime.js'

test('boots the C++ UI at 240x240 logical pixels', async ({ page }) => {
  page.on('console', (message) => console.log(`[browser:${message.type()}] ${message.text()}`))
  page.on('pageerror', (error) => console.log(`[browser:error] ${error.message}`))
  await page.goto('/')
  await expect(page.locator('[data-runtime-state="ready"]')).toBeVisible()
  await expect(page.locator('#picotracker-canvas')).toHaveAttribute('width', '240')
  await expect(page.locator('#picotracker-canvas')).toHaveAttribute('height', '240')
  await expect(page.locator('#picotracker-canvas')).toHaveAttribute('data-frame-content', 'rendered')
  await page.waitForTimeout(250)
  await page.locator('.audio-gate').evaluate((element) => { element.style.display = 'none' })
  await expect(page.locator('#picotracker-canvas')).toHaveScreenshot('device-boot.png')
})

test('stops the application thread before restarting the tracker canvas', async ({ page }) => {
  await page.goto('/')
  const ready = page.locator('[data-runtime-state="ready"]')
  await expect(ready).toBeVisible()

  await restartWorkbench(page)

  // A ready-only assertion can resolve against the old runtime before the
  // queued restart has even entered Stop. Prove both lifecycle edges.
  await ready.waitFor({ state: 'hidden', timeout: 10_000 })
  await expect(ready).toBeVisible({ timeout: 15_000 })
  await expect(page.locator('#picotracker-canvas')).toHaveAttribute('data-frame-content', 'rendered')
})

test('tool tray restart returns focus to its invoking control', async ({ page }) => {
  await page.goto('/?audio=disabled&dev=1')
  const ready = page.locator('[data-runtime-state="ready"]')
  const reset = page.locator('.tool-tray .reset')
  await expect(ready).toBeVisible()

  await reset.click()
  await ready.waitFor({ state: 'hidden', timeout: 10_000 })
  await expect(ready).toBeVisible({ timeout: 15_000 })
  await expect(reset).toBeFocused()
})

test('developer tool toggles expose state and regain focus after panel close', async ({ page }) => {
  await page.goto('/?audio=disabled&dev=1')
  await expect(page.locator('[data-runtime-state="ready"]')).toBeVisible()

  const toggle = page.getByRole('button', { name: 'Toggle Files tool' })
  await expect(toggle).toHaveAttribute('aria-pressed', 'false')
  await toggle.click()
  await expect(toggle).toHaveAttribute('aria-pressed', 'true')

  await page.getByRole('button', { name: 'Close Files tool' }).click()
  await expect(page.getByRole('region', { name: 'Files tool panel' })).toHaveCount(0)
  await expect(toggle).toHaveAttribute('aria-pressed', 'false')
  await expect(toggle).toBeFocused()
})
