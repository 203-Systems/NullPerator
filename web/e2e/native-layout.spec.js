import { expect, test } from '@playwright/test'
import { mkdirSync, readFileSync } from 'node:fs'
import { resolve } from 'node:path'

const screenshotRoot = process.env.NATIVE_LAYOUT_SCREENSHOT_DIR
  ? resolve(process.env.NATIVE_LAYOUT_SCREENSHOT_DIR)
  : null
const captureScreenshots = screenshotRoot !== null || process.env.NATIVE_LAYOUT_SCREENSHOTS === '1'
const goldenScreens = {
  main: `data:image/png;base64,${readFileSync(new URL('./ui2-golden.spec.js-snapshots/song.png', import.meta.url)).toString('base64')}`,
  playing: `data:image/png;base64,${readFileSync(new URL('./ui2-golden.spec.js-snapshots/topbar-playing.png', import.meta.url)).toString('base64')}`,
}

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

function union(boxes) {
  const left = Math.min(...boxes.map((box) => box.x))
  const top = Math.min(...boxes.map((box) => box.y))
  const right = Math.max(...boxes.map((box) => box.x + box.width))
  const bottom = Math.max(...boxes.map((box) => box.y + box.height))
  return { x: left, y: top, width: right - left, height: bottom - top }
}

function center(box) {
  return { x: box.x + box.width / 2, y: box.y + box.height / 2 }
}

async function installNativeBridge(page) {
  await page.addInitScript(() => {
    Object.defineProperty(navigator, 'requestMIDIAccess', {
      configurable: true,
      value: async () => ({ inputs: new Map(), outputs: new Map(), onstatechange: null }),
    })
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
                return Promise.resolve({
                  runtime: 'native-cpp',
                  platform: 'ios',
                  version: 1,
                  iosVersion: '1.0',
                  iosBuild: '1',
                  nullPeratorVersion: '2.3.0',
                  buildHash: '7092ad4a22c86c2b',
                  buildTime: '2026-09-01 11:10 CST',
                })
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

async function paintGoldenScreen(page, state) {
  await page.evaluate((source) => new Promise((resolveImage, rejectImage) => {
    const canvas = document.querySelector('#nullperator-canvas')
    const image = new Image()
    image.onload = () => {
      canvas.getContext('2d', { alpha: false }).drawImage(image, 0, 0, canvas.width, canvas.height)
      resolveImage()
    }
    image.onerror = rejectImage
    image.src = source
  }), goldenScreens[state])
}

async function saveScreenshot(page, testInfo, device, state) {
  if (!captureScreenshots) return
  if (!screenshotRoot) {
    await page.screenshot({ path: testInfo.outputPath(`${state}.png`) })
    return
  }
  mkdirSync(screenshotRoot, { recursive: true })
  const orientation = device.name.endsWith('landscape') ? 'landscape' : 'portrait'
  const deviceName = device.name
    .replace(/ (portrait|landscape)$/, '')
    .replaceAll(' ', '-')
    .toLowerCase()
  await page.screenshot({ path: resolve(screenshotRoot, `${deviceName}__${orientation}__${state}.png`) })
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
    if (captureScreenshots) await paintGoldenScreen(page, 'main')

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
    const actionBoxes = new Map()
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
      actionBoxes.set(action, box)
    }

    const controlsBox = await page.locator('.operator-controls').boundingBox()
    const directionBox = union(['up', 'left', 'down', 'right'].map((action) => actionBoxes.get(action)))
    const faceBox = union(['edit', 'option'].map((action) => actionBoxes.get(action)))
    const bottomBox = union(['play', 'shift'].map((action) => actionBoxes.get(action)))
    const viewportCenter = center(viewport)
    const controlsCenter = center(controlsBox)
    const directionCenter = center(directionBox)
    const faceCenter = center(faceBox)
    const bottomCenter = center(bottomBox)
    expect(Math.abs(controlsCenter.x - viewportCenter.x)).toBeLessThanOrEqual(1)
    expect(Math.abs((directionCenter.x + faceCenter.x) / 2 - viewportCenter.x)).toBeLessThanOrEqual(2)
    expect(Math.abs(bottomCenter.x - viewportCenter.x)).toBeLessThanOrEqual(2)
    if (device.width > device.height) {
      expect(Math.abs(directionCenter.y - faceCenter.y)).toBeLessThanOrEqual(2)
      expect(Math.abs(canvasBox.x + canvasBox.width / 2 - viewportCenter.x)).toBeLessThanOrEqual(1)
      expect(Math.abs(canvasBox.y + canvasBox.height / 2 - viewportCenter.y)).toBeLessThanOrEqual(1)
      expect(Math.abs(center(actionBoxes.get('play')).x - directionCenter.x)).toBeLessThanOrEqual(2)
      expect(Math.abs(center(actionBoxes.get('shift')).x - faceCenter.x)).toBeLessThanOrEqual(2)
    } else {
      expect(Math.abs(directionCenter.y - faceCenter.y)).toBeLessThanOrEqual(2)
    }

    const settings = page.getByRole('button', { name: 'Open settings' })
    const settingsBox = await settings.boundingBox()
    expect(settingsBox).not.toBeNull()
    expect(settingsBox.x).toBeGreaterThanOrEqual(0)
    expect(settingsBox.y).toBeGreaterThanOrEqual(0)
    expect(settingsBox.x + settingsBox.width).toBeLessThanOrEqual(device.width)
    expect(settingsBox.y + settingsBox.height).toBeLessThanOrEqual(device.height)
    expect(Math.abs(center(settingsBox).y - bottomCenter.y)).toBeLessThanOrEqual(2)
    for (const box of boxes) expect(intersects(settingsBox, box)).toBe(false)

    await saveScreenshot(page, testInfo, device, 'main')
    if (captureScreenshots) {
      await paintGoldenScreen(page, 'playing')
      await saveScreenshot(page, testInfo, device, 'playing')
      await paintGoldenScreen(page, 'main')
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

    await saveScreenshot(page, testInfo, device, 'settings')

    if (captureScreenshots) {
      await dialog.getByRole('button', { name: /^MIDI/ }).click()
      const midiDialog = page.getByRole('dialog', { name: 'ROUTE MAP' })
      await expect(midiDialog).toBeVisible()
      await saveScreenshot(page, testInfo, device, 'midi-route')

      await midiDialog.getByRole('button', { name: 'Back to settings' }).click()
      await expect(dialog).toBeVisible()
      await dialog.getByRole('button', { name: /^SOFTWARE VERSION/ }).click()
      await expect(dialog.getByText('BUILD HASH', { exact: true })).toBeVisible()
      await expect(dialog.getByText('BUILD TIME', { exact: true })).toBeVisible()
      await page.waitForTimeout(220)
      await saveScreenshot(page, testInfo, device, 'software-version')
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
