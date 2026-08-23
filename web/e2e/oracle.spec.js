import { expect, test } from '@playwright/test'

test('isolated WASM oracle matches deterministic 44.1 and 48 kHz render contracts', async ({ page }) => {
  await page.goto('/oracle.html')
  const result = await page.evaluate(async () => {
    const { instance } = await WebAssembly.instantiateStreaming(
      fetch('/wasm/picotracker-oracle.wasm'), {},
    )
    const memory = new Uint32Array(instance.exports.memory.buffer)
    const read = (rate) => {
      const pointer = instance.exports.PicoTracker_Wasm_GetRenderOracle(rate)
      return Array.from(memory.slice(pointer >>> 2, (pointer >>> 2) + 6))
    }
    return { native: read(44100), boundary: read(48000) }
  })

  expect(result.native).toEqual([1, 24, 44100, 128, 799941061, 16384])
  expect(result.boundary).toEqual([1, 24, 48000, 140, 2233655419, 16384])
})
