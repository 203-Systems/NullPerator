import { expect, test } from '@playwright/test'
import { readFileSync } from 'node:fs'
const bootstrap = readFileSync(new URL('../../android/app/src/main/native-bootstrap.js', import.meta.url), 'utf8')
for (const viewport of [{ width: 412, height: 915 }, { width: 915, height: 412 }]) {
  test(`Android bridge and settings ${viewport.width}x${viewport.height}`, async ({ page }) => {
    await page.setViewportSize(viewport)
    const errors = []
    page.on('pageerror', error => errors.push(error.message))
    await page.addInitScript(source => {
      globalThis.androidMessages = []
      globalThis.NullPeratorAndroid = { postMessage(text) {
        const message = JSON.parse(text)
        androidMessages.push(message)
        let result = true
        if (message.command === 'nativeReady') result = { runtime: 'native-cpp', platform: 'android', appVersion: '0.2', appBuild: 4 }
        if (message.command === 'nativeFrame') result = message.after === 1
          ? { version: 1, changed: false, sequence: 1 }
          : { version: 1, changed: true, sequence: 1, width: 240, height: 240,
              palette: btoa(String.fromCharCode(20, 30, 40).repeat(256)),
              regions: [{ x: 0, y: 0, width: 240, height: 240, indices: btoa('\0'.repeat(240*240)) }] }
        queueMicrotask(() => globalThis.__nullPeratorAndroidReply(message.id, result, null))
      } }
      // Execute the shipped bootstrap, before the application module starts.
      ;(0, eval)(source)
    }, bootstrap)
    await page.goto('/')
    const canvas = page.locator('#nullperator-canvas')
    await expect(canvas).toBeVisible()
    await expect.poll(() => canvas.evaluate(c => Array.from(c.getContext('2d').getImageData(0,0,1,1).data))).toEqual([20,30,40,255])
    await page.keyboard.press('ArrowRight')
    await expect.poll(() => page.evaluate(() => androidMessages.some(m => m.command === 'nativeAction' && m.action === 2 && m.pressed))).toBe(true)
    await page.getByRole('button', { name: 'Open settings' }).click()
    const settings = page.getByRole('dialog', { name: 'SETTINGS' })
    await expect(settings.getByText('0.2 (4)', { exact: true })).toBeVisible()
    await expect(settings.getByRole('button', { name: /^EXPORT FILES/ })).toBeVisible()
    await settings.getByRole('button', { name: /^EXPORT FILES/ }).click()
    await expect.poll(() => page.evaluate(() => androidMessages.some(m => m.command === 'openFiles'))).toBe(true)
    await expect.poll(async () => { const b = await settings.boundingBox(); return b.y + b.height }).toBeLessThanOrEqual(viewport.height)
    const bounds = await settings.boundingBox()
    expect(bounds.x).toBeGreaterThanOrEqual(0)
    expect(bounds.x + bounds.width).toBeLessThanOrEqual(viewport.width)
    expect(bounds.y + bounds.height).toBeLessThanOrEqual(viewport.height)
    expect(errors).toEqual([])
  })
}
