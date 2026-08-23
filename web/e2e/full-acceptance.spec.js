import { expect, test } from '@playwright/test'

const sections = [
  ['Device', 'PicoTracker Device'],
  ['Files', 'Files'],
  ['MIDI', 'Web MIDI'],
  ['Logs', 'Logs'],
  ['Trace', 'Performance Trace'],
  ['Settings', 'Settings'],
  ['About', 'About'],
]

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
  await expect(page.locator('[data-storage-state="ready"]')).toBeVisible()

  const canvas = page.locator('#picotracker-canvas')
  await expect(canvas).toHaveAttribute('width', '240')
  await expect(canvas).toHaveAttribute('height', '240')
  await expect(canvas).toHaveAttribute('data-frame-content', 'rendered')
  expect(await canvas.evaluate((element) => getComputedStyle(element).imageRendering)).toMatch(/pixelated|crisp-edges/)

  await expect(page.getByRole('navigation', { name: 'Workbench sections' }).getByRole('button'))
    .toHaveText(sections.map(([section]) => section))
  await expect(page.getByRole('button', { name: /HID/i })).toHaveCount(0)
  await expect(page.getByRole('button', { name: /Serial/i })).toHaveCount(0)

  for (const [section, heading] of sections) {
    await page.getByRole('button', { name: section, exact: true }).click()
    await expect(page.getByRole('heading', { name: heading, exact: true })).toBeVisible()
  }

  await page.getByRole('button', { name: 'Settings', exact: true }).click()
  await page.getByLabel('Device scale').selectOption('2')
  await page.getByLabel('Target buffer').selectOption('2048')
  await page.getByRole('slider', { name: 'Output volume' }).fill('73')

  await page.reload()
  await expect(page.locator('[data-runtime-state="ready"]')).toBeVisible({ timeout: 20_000 })
  await page.getByRole('button', { name: 'Settings', exact: true }).click()
  await expect(page.getByLabel('Device scale')).toHaveValue('2')
  await expect(page.getByLabel('Target buffer')).toHaveValue('2048')
  await expect(page.getByRole('slider', { name: 'Output volume' })).toHaveValue('73')

  await page.setViewportSize({ width: 820, height: 1180 })
  await expect(page.getByRole('navigation', { name: 'Workbench sections' })).toBeVisible()
  await page.getByRole('button', { name: 'Device', exact: true }).click()
  await expect(canvas).toBeInViewport()
  expect(remoteRuntimeRequests).toEqual([])

  await page.getByRole('button', { name: 'Stop runtime' }).click()
  await expect(page.locator('[data-runtime-state]')).toHaveAttribute(
    'data-runtime-state', 'idle', { timeout: 10_000 },
  )
})
