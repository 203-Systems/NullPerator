import { expect, test } from '@playwright/test'

const devices = [
  { name: 'iPhone Pro portrait', width: 402, height: 874 },
  { name: 'iPhone Pro landscape', width: 874, height: 402 },
  { name: 'iPhone Pro Max portrait', width: 440, height: 956 },
  { name: 'iPhone Pro Max landscape', width: 956, height: 440 },
  { name: 'iPad mini portrait', width: 744, height: 1133 },
  { name: 'iPad mini landscape', width: 1133, height: 744 },
  { name: 'iPad Air portrait', width: 820, height: 1180 },
  { name: 'iPad Air landscape', width: 1180, height: 820 },
  { name: 'iPad Pro 11 portrait', width: 834, height: 1194 },
  { name: 'iPad Pro 11 landscape', width: 1194, height: 834 },
  { name: 'iPad Pro 13 portrait', width: 1024, height: 1366 },
  { name: 'iPad Pro 13 landscape', width: 1366, height: 1024 },
]

function intersects(a, b) {
  return a.x < b.x + b.width
    && a.x + a.width > b.x
    && a.y < b.y + b.height
    && a.y + a.height > b.y
}

async function installNativeBridge(page) {
  await page.addInitScript(() => {
    globalThis.__nullPeratorNativeCore = true
    globalThis.__nullPeratorControllerState = { connected: false, count: 0, names: [] }
    globalThis.__nullPeratorNativeBattery = { percentage: 76, charging: false, available: true }
    globalThis.__nullPeratorIOSVersion = '1.0 (1)'
    globalThis.__nullPeratorFirmwareVersion = 'test'
    globalThis.webkit = {
      messageHandlers: {
        nullPeratorNative: {
          postMessage(message) {
            switch (message?.command) {
              case 'nativeReady':
                return Promise.resolve({ runtime: 'native-cpp', platform: 'ios', version: 1 })
              case 'nativeFrame':
                return Promise.resolve({ version: 1, changed: false })
              case 'nativeMidiDrain':
                return Promise.resolve({ packets: [], droppedNormal: 0, droppedRealtime: 0 })
              default:
                return Promise.resolve(null)
            }
          },
        },
      },
    }
  })
}

for (const device of devices) {
  test(`native UI fits ${device.name}`, async ({ page }, testInfo) => {
    await page.setViewportSize(device)
    await installNativeBridge(page)
    await page.goto('/')

    const app = page.getByRole('application', { name: 'NullPerator' })
    const canvas = page.locator('#nullperator-canvas')
    await expect(app).toBeVisible()
    await expect(canvas).toBeVisible()

    const viewport = { x: 0, y: 0, width: device.width, height: device.height }
    const canvasBox = await canvas.boundingBox()
    expect(canvasBox).not.toBeNull()
    expect(intersects(canvasBox, viewport)).toBe(true)
    expect(canvasBox.x).toBeGreaterThanOrEqual(0)
    expect(canvasBox.y).toBeGreaterThanOrEqual(0)
    expect(canvasBox.x + canvasBox.width).toBeLessThanOrEqual(device.width)
    expect(canvasBox.y + canvasBox.height).toBeLessThanOrEqual(device.height)

    const controls = page.locator('.operator-controls [data-action]')
    await expect(controls).toHaveCount(8)
    const boxes = []
    for (const control of await controls.all()) {
      const box = await control.boundingBox()
      expect(box).not.toBeNull()
      expect(box.width).toBeGreaterThanOrEqual(44)
      expect(box.height).toBeGreaterThanOrEqual(44)
      expect(box.x).toBeGreaterThanOrEqual(0)
      expect(box.y).toBeGreaterThanOrEqual(0)
      expect(box.x + box.width).toBeLessThanOrEqual(device.width)
      expect(box.y + box.height).toBeLessThanOrEqual(device.height)
      expect(intersects(box, canvasBox)).toBe(false)
      const action = await control.getAttribute('data-action')
      const hitAction = await page.evaluate(({ x, y }) => (
        document.elementFromPoint(x, y)?.closest?.('[data-action]')?.getAttribute('data-action') ?? null
      ), { x: box.x + box.width / 2, y: box.y + box.height / 2 })
      expect(hitAction).toBe(action)
      boxes.push(box)
    }

    const settings = page.getByRole('button', { name: 'Open settings' })
    const settingsBox = await settings.boundingBox()
    expect(settingsBox).not.toBeNull()
    expect(settingsBox.x).toBeGreaterThanOrEqual(0)
    expect(settingsBox.y).toBeGreaterThanOrEqual(0)
    expect(settingsBox.x + settingsBox.width).toBeLessThanOrEqual(device.width)
    expect(settingsBox.y + settingsBox.height).toBeLessThanOrEqual(device.height)
    for (const box of boxes) expect(intersects(settingsBox, box)).toBe(false)

    if (process.env.NATIVE_LAYOUT_SCREENSHOTS === '1') {
      await page.screenshot({ path: testInfo.outputPath('controls.png') })
    }

    await settings.click()
    const dialog = page.getByRole('dialog', { name: 'SETTINGS' })
    await expect(dialog).toBeVisible()
    await expect.poll(() => dialog.evaluate((element) => getComputedStyle(element).transform)).toBe('none')
    const dialogBox = await dialog.boundingBox()
    expect(dialogBox).not.toBeNull()
    expect(dialogBox.x).toBeGreaterThanOrEqual(0)
    expect(dialogBox.y).toBeGreaterThanOrEqual(0)
    expect(dialogBox.x + dialogBox.width).toBeLessThanOrEqual(device.width)
    expect(dialogBox.y + dialogBox.height).toBeLessThanOrEqual(device.height)

    const closeBox = await dialog.getByRole('button', { name: 'Close settings' }).boundingBox()
    expect(closeBox).not.toBeNull()
    expect(closeBox.width).toBeGreaterThanOrEqual(30)
    expect(closeBox.height).toBeGreaterThanOrEqual(30)

    if (process.env.NATIVE_LAYOUT_SCREENSHOTS === '1') {
      await page.screenshot({ path: testInfo.outputPath('settings.png') })
    }

    const documentGeometry = await page.evaluate(() => ({
      document: [document.documentElement.clientWidth, document.documentElement.scrollWidth,
        document.documentElement.clientHeight, document.documentElement.scrollHeight],
      body: [document.body.clientWidth, document.body.scrollWidth,
        document.body.clientHeight, document.body.scrollHeight],
    }))
    expect(documentGeometry).toEqual({
      document: [device.width, device.width, device.height, device.height],
      body: [device.width, device.width, device.height, device.height],
    })
  })
}
