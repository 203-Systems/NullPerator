import { readFileSync } from 'node:fs'
import { expect, it } from 'vitest'

it('injects the shared core release version into the Web build', () => {
  const header = readFileSync(new URL('../../sources/ProductVersion.h', import.meta.url), 'utf8')
  expect(__NULLPERATOR_PRODUCT_VERSION__).toMatch(/^\d+\.\d+(?:\.\d+)?$/)
  expect(header).toContain(`inline constexpr char Version[] = "${__NULLPERATOR_PRODUCT_VERSION__}";`)
})
