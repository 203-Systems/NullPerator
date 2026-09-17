import { expect, test } from '@playwright/test'

async function edge(page, key, pressed) {
  const canvas = page.locator('#picotracker-canvas')
  const before = Number(await canvas.getAttribute('data-action-generation'))
  await page.keyboard[pressed ? 'down' : 'up'](key)
  await expect.poll(async () => Number(await canvas.getAttribute('data-action-generation'))).toBeGreaterThan(before)
}

async function tap(page, key) {
  await edge(page, key, true)
  await edge(page, key, false)
}

async function cellColors(page) {
  return page.locator('#picotracker-canvas').evaluate(canvas => {
    const copy = document.createElement('canvas')
    copy.width = copy.height = 240
    const context = copy.getContext('2d')
    context.drawImage(canvas, 0, 0, 240, 240)
    return Array.from({ length: 11 }, (_, index) =>
      Array.from(context.getImageData(15 + index % 3 * 74, 44 + Math.floor(index / 3) * 23, 1, 1).data).join(','))
  })
}

test('Instrument type grid commits only on release and preserves modified-instrument confirmation', async ({ page }, testInfo) => {
  test.setTimeout(90_000)
  await page.goto('/?audio=disabled&views-test=1&inputDiagnostics=1')
  await expect(page.locator('[data-runtime-state="ready"]')).toBeVisible({ timeout: 20_000 })
  await page.evaluate(() => globalThis.__picoTrackerViewsTest.request(5))
  await expect.poll(() => page.evaluate(() => globalThis.__picoTrackerViewsTest.current())).toBe(5)
  await tap(page, 's') // Type row.
  await edge(page, 'k', true)
  await page.waitForTimeout(200)
  const selectedColor = (await cellColors(page))[0]
  expect(selectedColor).not.toBe((await cellColors(page))[1])
  const selected = index => expect.poll(async () => {
    const colors = await cellColors(page)
    return colors.flatMap((color, i) => color === selectedColor ? [i] : [])
  }).toEqual([index])

  // NONE, SAMPLE, MIDI / SID, OPAL, DRUM / STACK, CHIPTUNE, GB-WAVE / ...
  await tap(page, 'd'); await selected(1)
  await tap(page, 'd'); await selected(2)
  await tap(page, 'd'); await selected(3) // Row-major horizontal wrap.
  await tap(page, 's'); await selected(6)
  await tap(page, 's'); await selected(9)
  await page.locator('#picotracker-canvas').screenshot({ path: testInfo.outputPath('inst-select.png') })

  // OPTION cancels. Reopening must start at NONE, not a browsed candidate.
  await tap(page, 'j')
  await edge(page, 'k', false)
  await edge(page, 'k', true); await selected(0)
  await tap(page, 's'); await tap(page, 's'); await tap(page, 's')
  await selected(9)
  await edge(page, 'k', false) // Commit GB-PULSE.
  await edge(page, 'k', true); await selected(9)
  await edge(page, 'k', false) // Same type: no dialog.

  // Modify pulse duty, then browse a different type without destroying it.
  await tap(page, 's')
  await edge(page, 'k', true); await tap(page, 'd'); await edge(page, 'k', false)
  await tap(page, 'w')
  await edge(page, 'k', true); await selected(9)
  await tap(page, 'd'); await selected(10)
  await edge(page, 'k', false) // Existing Lose settings? dialog, default NO.
  await page.locator('#picotracker-canvas').screenshot({ path: testInfo.outputPath('type-change-confirmation.png') })
  await tap(page, 'k') // A fresh Enter must work immediately after release.
  await edge(page, 'k', true); await selected(9) // NO retained GB-PULSE.
  await tap(page, 'd'); await selected(10)
  await edge(page, 'k', false)
  await tap(page, 'd'); await tap(page, 'k') // YES.
  await edge(page, 'k', true); await selected(10)
  await tap(page, 'w'); await selected(7)
  await tap(page, 'd'); await selected(8)
  await tap(page, 's'); await selected(8) // Missing cell is not selectable.
  await tap(page, 'j'); await edge(page, 'k', false)
  await edge(page, 'k', true); await selected(10)
  await edge(page, 'k', false)
})

test('a type change and first parameter edit can share one native input batch', async ({ page }) => {
  await page.goto('/?audio=disabled&views-test=1&inputDiagnostics=1')
  await expect(page.locator('[data-runtime-state="ready"]')).toBeVisible({ timeout: 20_000 })
  await page.evaluate(() => globalThis.__picoTrackerViewsTest.request(5))
  await expect.poll(() => page.evaluate(() => globalThis.__picoTrackerViewsTest.current())).toBe(5)
  await page.locator('#picotracker-canvas').evaluate(canvas => {
    const key = (code, pressed) => canvas.dispatchEvent(new KeyboardEvent(pressed ? 'keydown' : 'keyup', {
      code, key: code.slice(-1).toLowerCase(), bubbles: true, cancelable: true,
    }))
    const tap = code => { key(code, true); key(code, false) }
    tap('KeyS') // Name -> Type.
    key('KeyK', true)
    for (let i = 0; i < 9; ++i) tap('KeyD')
    key('KeyK', false) // NONE -> GB-PULSE.
    tap('KeyS') // Must use the new field count before the next frame capture.
    key('KeyK', true); tap('KeyD'); key('KeyK', false) // Duty 50% -> 75%.
    tap('KeyW') // Back to Type.
    key('KeyK', true)
  })
  await page.waitForTimeout(300)
  const colors = await cellColors(page)
  expect(colors[9]).not.toBe(colors[0])
  const selectedColor = colors[9]
  const selected = index => expect.poll(async () => (await cellColors(page))
    .flatMap((color, i) => color === selectedColor ? [i] : [])).toEqual([index])
  await selected(9)
  await tap(page, 'd'); await selected(10)
  await edge(page, 'k', false)
  await tap(page, 'k') // NO: changing the edited duty must trigger confirmation.
  await edge(page, 'k', true); await selected(9)
  await edge(page, 'k', false)
})

test('switching to Sample enables Import within the same native input batch', async ({ page }) => {
  await page.goto('/?audio=disabled&views-test=1&inputDiagnostics=1')
  await expect(page.locator('[data-runtime-state="ready"]')).toBeVisible({ timeout: 20_000 })
  await page.evaluate(() => globalThis.__picoTrackerViewsTest.request(5))
  await expect.poll(() => page.evaluate(() => globalThis.__picoTrackerViewsTest.current())).toBe(5)
  await tap(page, 's') // Type row; also grants real user activation for the picker.
  const [chooser] = await Promise.all([
    page.waitForEvent('filechooser', { timeout: 5_000 }),
    page.locator('#picotracker-canvas').evaluate(canvas => {
      const key = (code, pressed) => canvas.dispatchEvent(new KeyboardEvent(pressed ? 'keydown' : 'keyup', {
        code, key: code.slice(-1).toLowerCase(), bubbles: true, cancelable: true,
      }))
      const tap = code => { key(code, true); key(code, false) }
      key('KeyK', true); tap('KeyD'); key('KeyK', false) // NONE -> SAMPLE.
      tap('KeyS') // Sample actions row.
      tap('KeyD') // BROWSE -> IMPORT, before CaptureInstrument sees the new type.
      tap('KeyK')
    }),
  ])
  await chooser.setFiles([])
})
