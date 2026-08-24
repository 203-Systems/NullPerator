import { createHash } from 'node:crypto'
import { readFile } from 'node:fs/promises'
import { dirname, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'
import { describe, expect, it } from 'vitest'

const here = dirname(fileURLToPath(import.meta.url))
const goldenDirectory = resolve(here, '../e2e/ui2-golden.spec.js-snapshots')

function sha256(value) {
  return createHash('sha256').update(value).digest('hex')
}

function pngDimensions(png) {
  const signature = Buffer.from([137, 80, 78, 71, 13, 10, 26, 10])
  expect(png.subarray(0, 8)).toEqual(signature)
  expect(png.subarray(12, 16).toString('ascii')).toBe('IHDR')
  return { width: png.readUInt32BE(16), height: png.readUInt32BE(20) }
}

describe('approved UI2 golden frames', () => {
  it('keeps every approved 240x240 state byte-for-byte locked', async () => {
    const manifest = JSON.parse(await readFile(resolve(goldenDirectory, 'manifest.json'), 'utf8'))
    expect(manifest.format).toBe(1)
    expect(manifest.frameCount).toBe(43)
    expect(manifest.frames).toHaveLength(manifest.frameCount)
    expect(new Set(manifest.frames.map((frame) => frame.view)).size).toBe(manifest.frameCount)

    for (const frame of manifest.frames) {
      const png = await readFile(resolve(goldenDirectory, frame.file))
      expect(pngDimensions(png), frame.view).toEqual({ width: 240, height: 240 })
      expect(sha256(png), frame.view).toBe(frame.sha256)
      expect(frame.width, frame.view).toBe(240)
      expect(frame.height, frame.view).toBe(240)
    }
  })
})
