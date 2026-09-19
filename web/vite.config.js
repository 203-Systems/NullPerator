import { readFileSync } from 'node:fs'
import { defineConfig } from 'vite'
import { svelte } from '@sveltejs/vite-plugin-svelte'

const versionHeader = readFileSync(new URL('../sources/ProductVersion.h', import.meta.url), 'utf8')
const productVersion = versionHeader.match(/inline constexpr char Version\[\] = "([^"]+)";/)?.[1]
if (!productVersion || !/^\d+\.\d+(?:\.\d+)?$/.test(productVersion)) throw new Error('Invalid shared product version')

const isolationHeaders = {
  'Cross-Origin-Opener-Policy': 'same-origin',
  'Cross-Origin-Embedder-Policy': 'require-corp',
}

export default defineConfig({
  plugins: [svelte()],
  define: { __NULLPERATOR_PRODUCT_VERSION__: JSON.stringify(productVersion) },
  server: { headers: isolationHeaders },
  preview: { headers: isolationHeaders },
  test: { exclude: ['e2e/**', 'node_modules/**', 'dist/**'] },
})
