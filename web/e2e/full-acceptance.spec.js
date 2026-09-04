import { expect, test } from '@playwright/test'
import { stopWorkbench } from './helpers/runtime.js'

const userSections = [
  ['Tracker', 'NullPerator Player'],
  ['Files', 'Files'],
  ['MIDI', 'MIDI'],
  ['Settings', 'Settings'],
]

const developerSections = [
  ['Logs', 'Logs'],
  ['Trace', 'Performance Trace'],
]

const userNavigation = userSections.map(([section]) => section)
const developerNavigation = ['Tracker', 'Files', 'MIDI', 'Logs', 'Trace', 'Settings']

async function expectNavigation(page, expected) {
  const buttons = page.getByRole('navigation', { name: 'Main navigation' }).getByRole('button')
  await expect(buttons).toHaveCount(expected.length)
  await expect.poll(() => buttons.evaluateAll((elements) => (
    elements.map((element) => element.getAttribute('aria-label'))
  ))).toEqual(expected)
}

async function canvasGeometry(canvas) {
  const box = await canvas.boundingBox()
  expect(box).not.toBeNull()
  return box
}

test('verified static bundle exposes the complete isolated workbench and persistent settings', async ({ page }) => {
  test.setTimeout(75_000)
  const remoteRuntimeRequests = []
  page.on('request', (request) => {
    const url = new URL(request.url())
    if (url.origin !== 'http://127.0.0.1:4173') remoteRuntimeRequests.push(request.url())
  })

  const response = await page.goto('/')
  expect(response?.headers()['cross-origin-opener-policy']).toBe('same-origin')
  expect(response?.headers()['cross-origin-embedder-policy']).toBe('require-corp')
  await expect(page.locator('[data-runtime-state="ready"]')).toBeVisible({ timeout: 20_000 })
  await expect(page).toHaveTitle('NullPerator')
  const dashboard = page.locator('.dashboard')
  const navigation = page.getByRole('navigation', { name: 'Main navigation' })
  const initialViewport = page.viewportSize()
  await expect(dashboard).toHaveAttribute('data-developer-mode', 'false')
  await expect(page.getByText('Ready', { exact: true })).toHaveCount(0)
  await expectNavigation(page, userNavigation)
  for (const [section] of developerSections) {
    await expect(navigation.getByRole('button', { name: section, exact: true })).toHaveCount(0)
  }

  const canvas = page.locator('#picotracker-canvas')
  await expect(canvas).toHaveAttribute('width', '240')
  await expect(canvas).toHaveAttribute('height', '240')
  await expect(canvas).toHaveAttribute('data-frame-content', 'rendered')
  expect(await canvas.evaluate((element) => getComputedStyle(element).imageRendering)).toMatch(/pixelated|crisp-edges/)
  const normalGeometry = await canvasGeometry(canvas)

  await expect(page.getByRole('button', { name: /HID/i })).toHaveCount(0)
  await expect(page.getByRole('button', { name: /Serial/i })).toHaveCount(0)

  for (const [section, heading] of userSections) {
    await navigation.getByRole('button', { name: section, exact: true }).click()
    await expect(page.getByRole('heading', { name: heading, exact: true })).toBeVisible()
  }
  await expect(page.getByRole('link', { name: 'GitHub repository', exact: true }))
    .toHaveAttribute('href', 'https://github.com/203-Systems/NullPerator')
  await expect(page.getByRole('link', { name: 'Third-party notices', exact: true }))
    .toHaveAttribute('href', '/THIRD_PARTY_NOTICES.md')
  await expect(page.getByText('0.1', { exact: true })).toBeVisible()

  const settingsButton = navigation.getByRole('button', { name: 'Settings', exact: true })
  await settingsButton.click()
  const developerToggle = page.getByRole('button', { name: 'Developer tools', exact: true })
  await expect(settingsButton).toHaveAttribute('aria-current', 'page')
  await expect(developerToggle).toHaveAttribute('aria-pressed', 'false')
  await developerToggle.click()
  await expect(dashboard).toHaveAttribute('data-developer-mode', 'true')
  await expect(page.getByText('Ready', { exact: true })).toBeVisible()
  await expect(settingsButton).toHaveAttribute('aria-current', 'page')
  await expect(page.getByRole('heading', { name: 'Settings', exact: true })).toBeVisible()
  await expect(page.locator('[aria-label="Developer build details"]')).toBeVisible()
  await expectNavigation(page, developerNavigation)

  for (const [section, heading] of developerSections) {
    await navigation.getByRole('button', { name: section, exact: true }).click()
    await expect(page.getByRole('heading', { name: heading, exact: true })).toBeVisible()
  }
  await navigation.getByRole('button', { name: 'Tracker', exact: true }).click()
  await expect(canvas).toBeVisible()
  expect(await canvasGeometry(canvas)).toEqual(normalGeometry)

  await settingsButton.click()
  await developerToggle.click()
  await expect(dashboard).toHaveAttribute('data-developer-mode', 'false')
  await expect(settingsButton).toHaveAttribute('aria-current', 'page')
  await expectNavigation(page, userNavigation)
  await navigation.getByRole('button', { name: 'Tracker', exact: true }).click()
  await expect(canvas).toBeVisible()
  expect(await canvasGeometry(canvas)).toEqual(normalGeometry)

  await page.setViewportSize({ width: 390, height: 844 })
  await expect(dashboard).toHaveAttribute('data-layout', 'compact')
  await expect(dashboard).toHaveAttribute('data-developer-mode', 'false')
  await expect(page.getByRole('button', { name: 'Open menu' })).toBeFocused()
  expect(await page.evaluate(() => JSON.parse(
    localStorage.getItem('picotracker.wasm.settings.v4'),
  ).developerMode)).toBe(false)
  await page.setViewportSize(initialViewport)
  await expect(dashboard).toHaveAttribute('data-layout', 'desktop')
  await expect(dashboard).toHaveAttribute('data-developer-mode', 'false')
  await expectNavigation(page, userNavigation)
  await expect(navigation.getByRole('button', { name: 'Tracker', exact: true })).toBeFocused()

  await navigation.getByRole('button', { name: 'Settings', exact: true }).click()
  await developerToggle.click()
  await expect(dashboard).toHaveAttribute('data-developer-mode', 'true')
  await page.setViewportSize({ width: 390, height: 844 })
  await expect(dashboard).toHaveAttribute('data-layout', 'compact')
  await expect(dashboard).toHaveAttribute('data-developer-mode', 'true')
  expect(await page.evaluate(() => JSON.parse(
    localStorage.getItem('picotracker.wasm.settings.v4'),
  ).developerMode)).toBe(true)
  await page.setViewportSize(initialViewport)
  await expect(dashboard).toHaveAttribute('data-layout', 'desktop')
  await expect(dashboard).toHaveAttribute('data-developer-mode', 'true')
  await expectNavigation(page, developerNavigation)

  await navigation.getByRole('button', { name: 'Settings', exact: true }).click()
  await page.getByLabel('Device scale').selectOption('2')
  await page.getByLabel('Target buffer').selectOption('2048')
  await page.getByRole('slider', { name: 'Output volume' }).fill('73')

  await page.reload()
  await expect(page.locator('[data-runtime-state="ready"]')).toBeVisible({ timeout: 20_000 })
  await expect(dashboard).toHaveAttribute('data-developer-mode', 'true')
  await page.getByRole('navigation', { name: 'Main navigation' }).getByRole('button', { name: 'Settings', exact: true }).click()
  await expect(page.getByLabel('Device scale')).toHaveValue('2')
  await expect(page.getByLabel('Target buffer')).toHaveValue('2048')
  await expect(page.getByRole('slider', { name: 'Output volume' })).toHaveValue('73')

  await page.setViewportSize({ width: 820, height: 1180 })
  await expect(dashboard).toHaveAttribute('data-developer-mode', 'true')
  await expect(page.getByRole('navigation', { name: 'Main navigation' })).toBeVisible()
  await expect(page.getByRole('navigation', { name: 'Main navigation' }).getByText('Tracker', { exact: true })).toBeVisible()
  await page.getByRole('button', { name: 'Tracker', exact: true }).click()
  await expect(canvas).toBeInViewport()
  expect(remoteRuntimeRequests).toEqual([])

  await stopWorkbench(page)
  await expect(page.locator('[data-runtime-state]')).toHaveAttribute(
    'data-runtime-state', 'idle', { timeout: 10_000 },
  )
})
