import { expect, test } from '@playwright/test'
import { restartWorkbench } from './helpers/runtime.js'

test('@visual boots the C++ UI at 240x240 logical pixels', async ({ page }) => {
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

test('settings restart preserves files and returns focus to its invoking control', async ({ page }) => {
  await page.addInitScript(() => {
    localStorage.setItem('picotracker.wasm.settings.v4', JSON.stringify({
      version: 4,
      lowLatencyAudio: false,
      developerMode: false,
    }))
  })
  await page.goto('/?audio=disabled')
  const ready = page.locator('[data-runtime-state="ready"]')
  await expect(ready).toBeVisible()

  await page.getByRole('button', { name: 'Settings', exact: true }).click()
  await expect(page.getByRole('heading', { name: 'Settings', exact: true })).toBeVisible()
  await expect(page.getByText('Restart the audio engine and interface without deleting your projects or files.')).toBeVisible()

  const restart = page.getByRole('button', { name: 'Restart NullPerator', exact: true })
  await restart.click()
  await ready.waitFor({ state: 'hidden', timeout: 10_000 })
  await expect(ready).toBeVisible({ timeout: 15_000 })
  await expect(restart).toBeFocused()
})

test('user tools stay available while developer tools add only diagnostics', async ({ page }) => {
  await page.goto('/?audio=disabled')
  await expect(page.locator('[data-runtime-state="ready"]')).toBeVisible()

  const dashboard = page.locator('.dashboard')
  const navigation = page.getByRole('navigation', { name: 'Main navigation' })
  await expect(dashboard).toHaveAttribute('data-developer-mode', 'false')
  for (const section of ['Files', 'MIDI', 'Settings']) {
    await expect(navigation.getByRole('button', { name: section, exact: true })).toBeVisible()
  }
  await expect(navigation.getByRole('button', { name: 'Logs', exact: true })).toHaveCount(0)
  await expect(navigation.getByRole('button', { name: 'Trace', exact: true })).toHaveCount(0)

  await navigation.getByRole('button', { name: 'Settings', exact: true }).click()
  const settingsHeading = page.getByRole('heading', { name: 'Settings', exact: true })
  const developerToggle = page.getByRole('button', { name: 'Developer tools', exact: true })
  await expect(page.getByRole('link', { name: 'GitHub repository', exact: true }))
    .toHaveAttribute('href', 'https://github.com/203-Systems/NullPerator')
  await expect(settingsHeading).toBeVisible()
  await developerToggle.click()

  await expect(dashboard).toHaveAttribute('data-developer-mode', 'true')
  await expect(settingsHeading).toBeVisible()
  await expect(navigation.getByRole('button', { name: 'Logs', exact: true })).toBeVisible()
  await expect(navigation.getByRole('button', { name: 'Trace', exact: true })).toBeVisible()
})

test('player settings control the virtual controls and browser fullscreen', async ({ page }) => {
  await page.addInitScript(() => {
    let fullscreenElement = null
    Object.defineProperty(document, 'fullscreenElement', {
      configurable: true,
      get: () => fullscreenElement,
    })
    Object.defineProperty(document, 'exitFullscreen', {
      configurable: true,
      value: async () => {
        fullscreenElement = null
        document.dispatchEvent(new Event('fullscreenchange'))
      },
    })
    Object.defineProperty(HTMLElement.prototype, 'requestFullscreen', {
      configurable: true,
      value: async function requestFullscreen() {
        fullscreenElement = this
        document.dispatchEvent(new Event('fullscreenchange'))
      },
    })
  })
  await page.goto('/?audio=disabled')
  await expect(page.locator('[data-runtime-state="ready"]')).toBeVisible()
  const navigation = page.getByRole('navigation', { name: 'Main navigation' })
  await navigation.getByRole('button', { name: 'Settings', exact: true }).click()

  const virtualControls = page.getByRole('button', { name: 'Show virtual controls' })
  await expect(virtualControls).toHaveAttribute('aria-pressed', 'true')
  await virtualControls.click()
  await expect(virtualControls).toHaveAttribute('aria-pressed', 'false')

  const enterFullscreen = page.getByRole('button', { name: 'Enter fullscreen' })
  await enterFullscreen.click()
  const exitFullscreen = page.getByRole('button', { name: 'Exit fullscreen' })
  await expect(exitFullscreen).toBeVisible()
  await exitFullscreen.click()
  await expect(enterFullscreen).toBeVisible()

  await navigation.getByRole('button', { name: 'Tracker', exact: true }).click()
  await expect(page.locator('[aria-label="NullPerator virtual controls"]')).toHaveCount(0)
  await expect(page.locator('.keyboard-helper')).toHaveCount(0)
  await page.reload()
  await expect(page.locator('[data-runtime-state="ready"]')).toBeVisible()
  await expect(page.locator('[aria-label="NullPerator virtual controls"]')).toHaveCount(0)
})

test('settings reports a connected standard game controller', async ({ page }) => {
  await page.addInitScript(() => {
    let gamepads = []
    Object.defineProperty(navigator, 'getGamepads', {
      configurable: true,
      value: () => gamepads,
    })
    globalThis.__connectTestController = () => {
      gamepads = [{
        index: 0,
        id: 'NullPerator Test Pad',
        connected: true,
        mapping: 'standard',
        axes: [0, 0],
        buttons: Array.from({ length: 16 }, () => ({ pressed: false, value: 0 })),
      }]
      globalThis.dispatchEvent(new Event('gamepadconnected'))
    }
    globalThis.__setTestControllerButton = (index, pressed) => {
      gamepads[0].buttons[index] = { pressed, value: pressed ? 1 : 0 }
    }
  })
  await page.goto('/?audio=disabled&inputDiagnostics=1')
  await expect(page.locator('[data-runtime-state="ready"]')).toBeVisible()
  await page.getByRole('button', { name: 'Settings', exact: true }).click()
  await expect(page.getByText('Not connected', { exact: true })).toBeVisible()

  await page.evaluate(() => globalThis.__connectTestController())
  await expect(page.getByText('Connected · NullPerator Test Pad', { exact: true })).toBeVisible()

  await page.getByRole('button', { name: 'Tracker', exact: true }).click()
  const canvas = page.locator('#picotracker-canvas')
  await page.evaluate(() => globalThis.__setTestControllerButton(0, true))
  await expect(canvas).toHaveAttribute('data-action-mask', String(1 << 6))
  await page.evaluate(() => globalThis.__setTestControllerButton(0, false))
  await expect(canvas).toHaveAttribute('data-action-mask', '0')
})

test('tracker keeps the display above its controls across desktop shapes', async ({ page }) => {
  await page.setViewportSize({ width: 1000, height: 658 })
  await page.goto('/?audio=disabled')
  await expect(page.locator('[data-runtime-state="ready"]')).toBeVisible()
  const screen = page.locator('.screen-bezel')
  const controls = page.locator('.operator-controls')

  const shortScreen = await screen.boundingBox()
  const shortControls = await controls.boundingBox()
  expect(shortControls.y).toBeGreaterThan(shortScreen.y + shortScreen.height)

  await page.setViewportSize({ width: 820, height: 1000 })
  const tallScreen = await screen.boundingBox()
  const tallControls = await controls.boundingBox()
  expect(tallControls.y).toBeGreaterThan(tallScreen.y + tallScreen.height)
})

test('short desktop pins Settings while keyboard navigation scrolls the main destinations', async ({ page }) => {
  await page.addInitScript(() => {
    localStorage.setItem('picotracker.wasm.settings.v4', JSON.stringify({
      version: 4,
      developerMode: true,
    }))
  })
  await page.setViewportSize({ width: 1024, height: 260 })
  await page.goto('/?audio=disabled')
  await expect(page.locator('[data-runtime-state="ready"]')).toBeVisible()

  const navigation = page.getByRole('navigation', { name: 'Main navigation' })
  const mainDestinations = navigation.locator('.nav-group')
  const settings = navigation.getByRole('button', { name: 'Settings', exact: true })
  await expect(settings).toBeVisible()
  expect(await mainDestinations.evaluate((element) => element.scrollHeight > element.clientHeight)).toBe(true)

  const navigationBox = await navigation.boundingBox()
  const settingsBox = await settings.boundingBox()
  expect(navigationBox).not.toBeNull()
  expect(settingsBox).not.toBeNull()
  expect(settingsBox.y + settingsBox.height).toBeLessThanOrEqual(navigationBox.y + navigationBox.height)

  const tracker = navigation.getByRole('button', { name: 'Tracker', exact: true })
  await tracker.focus()
  for (const section of ['Files', 'MIDI', 'Logs', 'Trace', 'Settings']) {
    await page.keyboard.press('Tab')
    const destination = navigation.getByRole('button', { name: section, exact: true })
    await expect(destination).toBeFocused()
    if (section === 'Trace') {
      expect(await mainDestinations.evaluate((element) => element.scrollTop)).toBeGreaterThan(0)
      const groupBox = await mainDestinations.boundingBox()
      const destinationBox = await destination.boundingBox()
      expect(groupBox).not.toBeNull()
      expect(destinationBox).not.toBeNull()
      expect(destinationBox.y).toBeGreaterThanOrEqual(groupBox.y)
      expect(destinationBox.y + destinationBox.height).toBeLessThanOrEqual(groupBox.y + groupBox.height)
    }
  }
})
