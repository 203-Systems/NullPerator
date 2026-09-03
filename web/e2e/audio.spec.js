import { expect, test } from '@playwright/test'
import { stopWorkbench } from './helpers/runtime.js'

const setupWatchdogTimeoutMs = 12_000
const workletMode = process.env.PICOTRACKER_AUDIO_E2E === '1'

async function loadLockedRuntime(page) {
  await page.goto('/?inputDiagnostics=1')
  await expect(page.locator('.top-bar')).toBeVisible()
  await expect(page.locator('[data-runtime-state="ready"]')).toBeVisible({ timeout: 20_000 })
  const diagnostics = page.locator('.audio-diagnostics')
  return diagnostics
}

test('AudioWorklet is the default and presents a user-facing sound gate', async ({ page }) => {
  test.setTimeout(45_000)
  const browserDiagnostics = []
  page.on('console', (message) => browserDiagnostics.push(`console ${message.type()}: ${message.text()}`))
  page.on('pageerror', (error) => browserDiagnostics.push(`pageerror: ${error.message}`))
  page.on('requestfailed', (request) => browserDiagnostics.push(
    `requestfailed ${request.url()}: ${request.failure()?.errorText ?? 'unknown'}`,
  ))
  let diagnostics
  try {
    diagnostics = await loadLockedRuntime(page)
  } catch (error) {
    const snapshot = await page.evaluate(() => ({
      text: document.body.innerText,
      runtime: document.querySelector('.dashboard')?.getAttribute('data-runtime-state'),
    })).catch(() => ({}))
    throw new Error(`${error.message}\nBrowser diagnostics: ${browserDiagnostics.join(' | ')}\nSnapshot: ${JSON.stringify(snapshot)}`)
  }
  try {
    const capability = await diagnostics.getAttribute('data-audio-capability')
    expect(capability).toBe('available')
    await expect(page.locator('[data-audio-state="locked"]')).toBeVisible()
    const gate = page.getByRole('dialog', { name: 'Enable sound' })
    await expect(gate).toBeVisible()
    await expect.poll(() => gate.evaluate((element) => element.matches(':modal'))).toBe(true)
    const enableSound = gate.getByRole('button', { name: 'Enable sound' })
    await expect(enableSound).toBeFocused()
    await expect(page.locator('.operator-device')).toHaveAttribute('inert', '')
    if (!workletMode) return

    await enableSound.click()
    try {
      await expect(page.locator('[data-audio-state="running"]')).toBeVisible({ timeout: setupWatchdogTimeoutMs })
    } catch (error) {
      const snapshot = await diagnostics.evaluate((element) => ({ ...element.dataset })).catch(() => ({}))
      throw new Error(`${error.message}\nAudio diagnostics: ${JSON.stringify(snapshot)}\nBrowser diagnostics: ${browserDiagnostics.join(' | ')}`)
    }
    await expect(page.locator('#picotracker-canvas')).toBeFocused()
    const callbacks = page.locator('[data-audio-worklet-callbacks]')
    await expect(callbacks).not.toHaveAttribute('data-audio-worklet-callbacks', '0')
    const before = Number(await callbacks.getAttribute('data-audio-worklet-callbacks'))
    const underruns = page.locator('[data-audio-underruns]')
    const beforeUnderruns = await underruns.getAttribute('data-audio-underruns')
    const beforeProcessingMisses = await diagnostics.getAttribute('data-audio-processing-deadline-misses')
    await expect.poll(async () => Number(await diagnostics.getAttribute('data-audio-callback-micros'))).toBeGreaterThan(0)
    await expect.poll(async () => Number(await diagnostics.getAttribute('data-audio-callback-max-micros'))).toBeGreaterThan(0)
    await expect.poll(async () => Number(await diagnostics.getAttribute('data-audio-processing-deadline-micros'))).toBeGreaterThan(0)
    await page.waitForTimeout(500)
    await expect.poll(() => callbacks.getAttribute('data-audio-worklet-callbacks')).not.toBe(String(before))
    await expect(underruns).toHaveAttribute('data-audio-underruns', beforeUnderruns ?? '0')
    await expect(diagnostics).toHaveAttribute('data-audio-processing-deadline-misses', beforeProcessingMisses ?? '0')
  } finally {
    await stopWorkbench(page)
    await expect(page.locator('[data-runtime-state="idle"]')).toBeVisible({ timeout: 10_000 })
  }
})
