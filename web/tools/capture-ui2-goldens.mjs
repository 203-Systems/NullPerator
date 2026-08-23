import { createHash } from 'node:crypto'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { basename, resolve } from 'node:path'
import { chromium } from '@playwright/test'

function parseArguments(argv) {
  const values = new Map()
  for (let index = 0; index < argv.length; index += 1) {
    const argument = argv[index]
    if (!argument.startsWith('--')) continue
    const value = argv[index + 1]
    if (value && !value.startsWith('--')) {
      values.set(argument, value)
      index += 1
    } else {
      values.set(argument, true)
    }
  }
  return values
}

function sha256(value) {
  return createHash('sha256').update(value).digest('hex')
}

function decodeDataUrl(dataUrl) {
  const marker = 'base64,'
  const offset = dataUrl.indexOf(marker)
  if (offset < 0) throw new Error('Canvas did not return a base64 PNG data URL')
  return Buffer.from(dataUrl.slice(offset + marker.length), 'base64')
}

const args = parseArguments(process.argv.slice(2))
if (args.get('--approve') !== 'yes') {
  throw new Error('Refusing to replace approved references without --approve yes')
}

const sourceArgument = args.get('--source')
if (typeof sourceArgument !== 'string') {
  throw new Error('Usage: capture-ui2-goldens.mjs --source <approved.html> --approve yes [--output <directory>]')
}

const sourcePath = resolve(sourceArgument)
const outputPath = resolve(args.get('--output') || 'e2e/ui2-golden.spec.js-snapshots')
const fragment = await readFile(sourcePath, 'utf8')
await mkdir(outputPath, { recursive: true })

const browser = await chromium.launch({ channel: 'chrome', headless: true })
const page = await browser.newPage({ viewport: { width: 1024, height: 768 }, deviceScaleFactor: 1 })
const pageErrors = []
page.on('pageerror', (error) => pageErrors.push(error.message))
await page.setContent(fragment, { waitUntil: 'load' })

const frames = await page.locator('.pt-frame canvas.pt-code-stage').evaluateAll((canvases) => canvases.map((canvas) => {
  const frame = canvas.closest('.pt-frame')
  const caption = frame?.querySelector('figcaption')?.textContent?.trim() || ''
  return {
    view: canvas.dataset.view,
    group: frame?.dataset.group || '',
    label: caption,
    width: canvas.width,
    height: canvas.height,
    png: canvas.toDataURL('image/png'),
  }
}))

await browser.close()
if (pageErrors.length) throw new Error(pageErrors.join('\n'))
if (frames.length === 0) throw new Error('Approved reference contains no UI frames')

const seen = new Set()
const manifestFrames = []
for (const frame of frames) {
  if (!frame.view || seen.has(frame.view)) throw new Error(`Missing or duplicate data-view: ${frame.view}`)
  if (frame.width !== 240 || frame.height !== 240) {
    throw new Error(`${frame.view} is ${frame.width}x${frame.height}, expected 240x240`)
  }
  seen.add(frame.view)
  const png = decodeDataUrl(frame.png)
  const filename = `${frame.view}.png`
  await writeFile(resolve(outputPath, filename), png)
  manifestFrames.push({
    view: frame.view,
    group: frame.group,
    label: frame.label,
    file: filename,
    width: frame.width,
    height: frame.height,
    sha256: sha256(png),
  })
}

const manifest = {
  format: 1,
  source: basename(sourcePath),
  sourceSha256: sha256(fragment),
  frameCount: manifestFrames.length,
  frames: manifestFrames,
}
await writeFile(resolve(outputPath, 'manifest.json'), `${JSON.stringify(manifest, null, 2)}\n`)
process.stdout.write(`${JSON.stringify({ outputPath, frameCount: frames.length, sourceSha256: manifest.sourceSha256 })}\n`)
