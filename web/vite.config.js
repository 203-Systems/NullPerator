import { defineConfig } from 'vite'
import { svelte } from '@sveltejs/vite-plugin-svelte'

const isolationHeaders = {
  'Cross-Origin-Opener-Policy': 'same-origin',
  'Cross-Origin-Embedder-Policy': 'require-corp',
}

export default defineConfig({
  plugins: [svelte()],
  server: { headers: isolationHeaders },
  preview: { headers: isolationHeaders },
})
