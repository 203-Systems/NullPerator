import { readFile } from 'node:fs/promises'
import { inflateSync } from 'node:zlib'

const args = new Map()
for (let index = 2; index < process.argv.length; index += 2) {
  args.set(process.argv[index], process.argv[index + 1])
}

const actualPath = args.get('--actual')
const goldenPath = args.get('--golden')
if (!actualPath || !goldenPath) {
  throw new Error('Usage: compare-ui2-snapshot.mjs --actual frame.ppm --golden frame.png [--tolerance-region x,y,w,h,maxDelta]')
}

const paeth = (left, up, upperLeft) => {
  const estimate = left + up - upperLeft
  const leftDistance = Math.abs(estimate - left)
  const upDistance = Math.abs(estimate - up)
  const upperLeftDistance = Math.abs(estimate - upperLeft)
  if (leftDistance <= upDistance && leftDistance <= upperLeftDistance) return left
  return upDistance <= upperLeftDistance ? up : upperLeft
}

const decodePng = (buffer) => {
  const signature = '89504e470d0a1a0a'
  if (buffer.subarray(0, 8).toString('hex') !== signature) throw new Error('Invalid PNG signature')
  let offset = 8
  let width = 0
  let height = 0
  const compressed = []
  while (offset < buffer.length) {
    const length = buffer.readUInt32BE(offset)
    const type = buffer.toString('ascii', offset + 4, offset + 8)
    const data = buffer.subarray(offset + 8, offset + 8 + length)
    if (type === 'IHDR') {
      width = data.readUInt32BE(0)
      height = data.readUInt32BE(4)
      if (data[8] !== 8 || data[9] !== 6 || data[10] !== 0 || data[11] !== 0 || data[12] !== 0) {
        throw new Error('Golden PNG must be non-interlaced 8-bit RGBA')
      }
    } else if (type === 'IDAT') compressed.push(data)
    else if (type === 'IEND') break
    offset += 12 + length
  }
  const encoded = inflateSync(Buffer.concat(compressed))
  const bytesPerPixel = 4
  const stride = width * bytesPerPixel
  const rgba = Buffer.alloc(width * height * bytesPerPixel)
  let source = 0
  for (let y = 0; y < height; y += 1) {
    const filter = encoded[source++]
    for (let x = 0; x < stride; x += 1) {
      const raw = encoded[source++]
      const output = y * stride + x
      const left = x >= bytesPerPixel ? rgba[output - bytesPerPixel] : 0
      const up = y > 0 ? rgba[output - stride] : 0
      const upperLeft = y > 0 && x >= bytesPerPixel ? rgba[output - stride - bytesPerPixel] : 0
      if (filter === 0) rgba[output] = raw
      else if (filter === 1) rgba[output] = (raw + left) & 0xff
      else if (filter === 2) rgba[output] = (raw + up) & 0xff
      else if (filter === 3) rgba[output] = (raw + Math.floor((left + up) / 2)) & 0xff
      else if (filter === 4) rgba[output] = (raw + paeth(left, up, upperLeft)) & 0xff
      else throw new Error(`Unsupported PNG filter ${filter}`)
    }
  }
  return { width, height, rgba }
}

const decodePpm = (buffer) => {
  let offset = 0
  const token = () => {
    while (offset < buffer.length && /\s/.test(String.fromCharCode(buffer[offset]))) offset += 1
    if (buffer[offset] === 35) {
      while (offset < buffer.length && buffer[offset] !== 10) offset += 1
      return token()
    }
    const start = offset
    while (offset < buffer.length && !/\s/.test(String.fromCharCode(buffer[offset]))) offset += 1
    return buffer.toString('ascii', start, offset)
  }
  if (token() !== 'P6') throw new Error('Actual snapshot must be binary PPM P6')
  const width = Number(token())
  const height = Number(token())
  if (Number(token()) !== 255) throw new Error('Actual PPM must use max value 255')
  while (offset < buffer.length && /\s/.test(String.fromCharCode(buffer[offset]))) offset += 1
  const rgb = buffer.subarray(offset)
  if (rgb.length !== width * height * 3) throw new Error('Actual PPM pixel payload has the wrong size')
  return { width, height, rgb }
}

const parseTolerance = (value) => {
  if (!value) return null
  const numbers = value.split(',').map(Number)
  if (numbers.length !== 5 || numbers.some(number => !Number.isInteger(number) || number < 0)) {
    throw new Error('Tolerance region must be x,y,width,height,maxDelta')
  }
  const [x, y, width, height, maxDelta] = numbers
  return { x, y, width, height, maxDelta }
}

const tolerance = parseTolerance(args.get('--tolerance-region'))
const [actual, golden] = await Promise.all([
  readFile(actualPath).then(decodePpm),
  readFile(goldenPath).then(decodePng),
])
if (actual.width !== golden.width || actual.height !== golden.height) {
  throw new Error(`Dimension mismatch: actual ${actual.width}x${actual.height}, golden ${golden.width}x${golden.height}`)
}

let rawMismatch = 0
let effectiveMismatch = 0
let maximumDelta = 0
for (let y = 0; y < actual.height; y += 1) {
  for (let x = 0; x < actual.width; x += 1) {
    const pixel = y * actual.width + x
    let delta = 0
    for (let channel = 0; channel < 3; channel += 1) {
      delta = Math.max(delta, Math.abs(actual.rgb[pixel * 3 + channel] - golden.rgba[pixel * 4 + channel]))
    }
    if (delta === 0) continue
    rawMismatch += 1
    maximumDelta = Math.max(maximumDelta, delta)
    const allowed = tolerance && x >= tolerance.x && x < tolerance.x + tolerance.width &&
      y >= tolerance.y && y < tolerance.y + tolerance.height ? tolerance.maxDelta : 0
    if (delta > allowed) effectiveMismatch += 1
  }
}

const result = {
  actual: actualPath,
  golden: goldenPath,
  width: actual.width,
  height: actual.height,
  rawMismatch,
  effectiveMismatch,
  maximumDelta,
  tolerance,
  passed: effectiveMismatch === 0,
}
console.log(JSON.stringify(result))
if (!result.passed) process.exitCode = 1
