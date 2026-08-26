import { existsSync, readFileSync, readdirSync, statSync } from 'node:fs'
import { dirname, extname, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'

const webRoot = resolve(dirname(fileURLToPath(import.meta.url)), '..')
const distRoot = resolve(webRoot, process.argv[2] ?? 'dist')

const requiredFiles = [
  'index.html',
  'oracle.html',
  '_headers',
  'THIRD_PARTY_NOTICES.md',
  'wasm/picotracker.js',
  'wasm/picotracker.wasm',
  'wasm/picotracker-oracle.wasm',
]

function fail(message) {
  throw new Error(`dist verification failed: ${message}`)
}

function relativeFiles(directory, prefix = '') {
  return readdirSync(directory, { withFileTypes: true }).flatMap((entry) => {
    const relative = prefix ? `${prefix}/${entry.name}` : entry.name
    return entry.isDirectory()
      ? relativeFiles(resolve(directory, entry.name), relative)
      : [relative]
  })
}

function isRemote(reference) {
  return /^(?:https?:)?\/\//i.test(reference)
}

function isEmbedded(reference) {
  return /^(?:data|blob|mailto|tel):/i.test(reference) || reference.startsWith('#')
}

function cleanReference(reference) {
  return decodeURIComponent(reference.split(/[?#]/, 1)[0])
}

function assertLocalReference(owner, reference) {
  if (!reference || isEmbedded(reference)) return
  if (isRemote(reference)) fail(`${owner} has remote runtime dependency ${reference}`)

  const cleaned = cleanReference(reference)
  const target = cleaned.startsWith('/')
    ? resolve(distRoot, `.${cleaned}`)
    : resolve(dirname(resolve(distRoot, owner)), cleaned)

  if (!target.startsWith(`${distRoot}/`) || !existsSync(target)) {
    fail(`${owner} references missing or escaping asset ${reference}`)
  }
}

function verifyHtml(relative) {
  const source = readFileSync(resolve(distRoot, relative), 'utf8')
  const attributes = /\b(?:src|href)\s*=\s*["']([^"']+)["']/gi
  for (const match of source.matchAll(attributes)) assertLocalReference(relative, match[1])
  if (relative === 'index.html' && !/<script\b[^>]*type=["']module["']/i.test(source)) {
    fail('index.html has no module entrypoint')
  }
}

function verifyCss(relative) {
  const source = readFileSync(resolve(distRoot, relative), 'utf8')
  const references = /(?:url\(\s*|@import\s+)["']?([^"')\s;]+)["']?\s*\)?/gi
  for (const match of source.matchAll(references)) assertLocalReference(relative, match[1])
}

function verifyJavaScript(relative) {
  const source = readFileSync(resolve(distRoot, relative), 'utf8')
  const remoteRuntimeCall = /\b(?:fetch|importScripts)\s*\(\s*["'`]https?:\/\/|\b(?:new\s+Worker|addModule)\s*\(\s*["'`]https?:\/\//i
  const remoteModule = /\b(?:import|export)\s+(?:[^"'`]*?\sfrom\s*)?["'`]https?:\/\//i
  const dynamicRemoteModule = /\bimport\s*\(\s*["'`]https?:\/\//i
  if (remoteRuntimeCall.test(source) || remoteModule.test(source) || dynamicRemoteModule.test(source)) {
    fail(`${relative} contains a remote runtime dependency`)
  }
}

function verifyWasm(relative) {
  const path = resolve(distRoot, relative)
  const source = readFileSync(path)
  if (source.length < 1024) fail(`${relative} is unexpectedly small (${source.length} bytes)`)
  if (!source.subarray(0, 4).equals(Buffer.from([0x00, 0x61, 0x73, 0x6d]))) {
    fail(`${relative} does not have a WebAssembly magic header`)
  }
}

function requireHeader(headers, pathPattern, name, value) {
  const escapedPath = pathPattern.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')
  const escapedName = name.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')
  const escapedValue = value.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')
  const block = new RegExp(`(?:^|\\n)${escapedPath}\\s*\\n((?:[ \\t]+[^\\n]+\\n?)*)`, 'i').exec(headers)
  if (!block || !new RegExp(`^[ \\t]+${escapedName}:\\s*${escapedValue}\\s*$`, 'im').test(block[1])) {
    fail(`_headers is missing ${name}: ${value} for ${pathPattern}`)
  }
}

if (!existsSync(distRoot) || !statSync(distRoot).isDirectory()) {
  fail(`missing dist directory ${distRoot}`)
}
for (const relative of requiredFiles) {
  if (!existsSync(resolve(distRoot, relative))) fail(`missing ${relative}`)
}

const files = relativeFiles(distRoot)
if (!files.some((file) => /^assets\/.+[.-][a-z0-9_-]{6,}\.js$/i.test(file))) {
  fail('missing hashed JavaScript application asset')
}

for (const relative of files) {
  switch (extname(relative)) {
    case '.html': verifyHtml(relative); break
    case '.css': verifyCss(relative); break
    case '.js': verifyJavaScript(relative); break
    case '.wasm': verifyWasm(relative); break
  }
}

// The product is emitted as an Emscripten JS loader plus WASM. The isolated
// render oracle deliberately uses STANDALONE_WASM and is instantiated directly
// by oracle.spec.js, so it has no generated JavaScript companion to verify.
for (const loader of ['wasm/picotracker.js']) {
  const wasmName = loader.replace(/\.js$/, '.wasm').split('/').at(-1)
  if (!readFileSync(resolve(distRoot, loader), 'utf8').includes(wasmName)) {
    fail(`${loader} does not reference its local ${wasmName}`)
  }
}

const headers = readFileSync(resolve(distRoot, '_headers'), 'utf8')
requireHeader(headers, '/*', 'Cross-Origin-Opener-Policy', 'same-origin')
requireHeader(headers, '/*', 'Cross-Origin-Embedder-Policy', 'require-corp')
requireHeader(headers, '/wasm/*.wasm', 'Content-Type', 'application/wasm')
requireHeader(headers, '/assets/*', 'Cache-Control', 'public, max-age=31536000, immutable')

const notices = readFileSync(resolve(distRoot, 'THIRD_PARTY_NOTICES.md'), 'utf8')
for (const project of ['MatrixOS WebUI', 'Svelte', 'Vite', 'fflate', 'Emscripten', 'SDL 2']) {
  if (!notices.includes(project)) fail(`third-party notices omit ${project}`)
}

console.log(`Verified static PicoTracker WASM distribution: ${files.length} files`)
