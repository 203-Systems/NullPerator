import { createHash } from 'node:crypto'
import { readFileSync } from 'node:fs'
import { resolve } from 'node:path'

import { expect, test } from '@playwright/test'
import { zipSync } from 'fflate'
import { restartWorkbench, stopWorkbench } from './helpers/runtime.js'

const factoryRoot = process.env.PICOTRACKER_FACTORY_CONTENT
const workletMode = process.env.PICOTRACKER_AUDIO_E2E === '1'
const editableSampleName = 'ZZ_EDIT.wav'
const editableProjectSamplePath = `/data/projects/oneCycAc/samples/${editableSampleName}`
const factoryProjectPath = '/data/projects/oneCycAc/lgptsav.dat'
const factoryProjectSamplePath = '/data/projects/oneCycAc/samples/AKWF_0906.wav'
const editableLibrarySamplePath = `/data/samples/${editableSampleName}`
const editableSampleJournalLeaf = Buffer.from(editableSampleName).toString('hex').toUpperCase()
const editableWorkingSamplePath =
  `/data/projects/oneCycAc/samples/.sample-editor-working-copy-${editableSampleJournalLeaf}`
const editableBackupSamplePath =
  `/data/projects/oneCycAc/samples/.sample-editor-backup-copy-${editableSampleJournalLeaf}`

const fixtureFiles = Object.freeze([
  {
    archivePath: '.current',
    sourcePath: 'default-current.txt',
    size: 8,
    sha256: '7e73e6f6eef10ba722f6f679829dec22e3a5013caa7b3435e5abdcc1f9b610ad',
  },
  {
    archivePath: 'projects/oneCycAc/lgptsav.dat',
    sourcePath: 'projects/oneCycAc/lgptsav.dat',
    size: 81_175,
    sha256: 'f76d7ea7fa4a1ed7ae28505fec6109056e9c0d1c06ca25a461989883f08e2cab',
  },
  {
    archivePath: 'projects/oneCycAc/samples/AKWF_0906.wav',
    sourcePath: 'projects/oneCycAc/samples/AKWF_0906.wav',
    size: 1_344,
    sha256: '29fdcece41ba601ece5409edb50161fd32a221a1e4bedc45d5afd4ec24fcde5c',
  },
  {
    archivePath: `samples/${editableSampleName}`,
    sourcePath: 'projects/oneCycAc/samples/AKWF_0906.wav',
    size: 1_344,
    sha256: '29fdcece41ba601ece5409edb50161fd32a221a1e4bedc45d5afd4ec24fcde5c',
  },
])

test.skip(!factoryRoot, 'Set PICOTRACKER_FACTORY_CONTENT to run the real factory-content acceptance gate')

function sha256(bytes) {
  return createHash('sha256').update(bytes).digest('hex')
}

function expectedOneFrameTrim(bytes) {
  const source = Buffer.from(bytes)
  expect(source.toString('ascii', 0, 4)).toBe('RIFF')
  expect(source.toString('ascii', 8, 12)).toBe('WAVE')
  expect(source.toString('ascii', 36, 40)).toBe('data')
  const channels = source.readUInt16LE(22)
  const bitsPerSample = source.readUInt16LE(34)
  const bytesPerFrame = channels * (bitsPerSample / 8)
  const dataBytes = source.readUInt32LE(40)
  expect(bytesPerFrame).toBeGreaterThan(0)
  expect(dataBytes).toBeGreaterThan(bytesPerFrame)
  expect(dataBytes % bytesPerFrame).toBe(0)

  // SampleEditor trim is deliberately exercised with start=1 and the original
  // final frame selected. Its in-place writer shifts PCM left by one frame and
  // patches the canonical 44-byte WAV header.
  const result = Buffer.from(source)
  source.copy(result, 44, 44 + bytesPerFrame, 44 + dataBytes)
  const trimmedDataBytes = dataBytes - bytesPerFrame
  result.writeUInt32LE(44 + trimmedDataBytes - 8, 4)
  result.writeUInt32LE(trimmedDataBytes, 40)
  return result
}

function loadFactoryFixture() {
  const archive = {}
  for (const file of fixtureFiles) {
    const bytes = readFileSync(resolve(factoryRoot, file.sourcePath))
    expect(bytes).toHaveLength(file.size)
    expect(sha256(bytes)).toBe(file.sha256)
    archive[file.archivePath] = new Uint8Array(bytes)
  }
  return zipSync(archive, { level: 0 })
}

function loadLegacyMidiFixture() {
  const midiProject = readFileSync(resolve(factoryRoot, 'projects/bt9-midi/lgptsav.dat'))
  const sampleProject = readFileSync(resolve(factoryRoot, 'projects/oneCycAc/lgptsav.dat'))
  const sample = readFileSync(resolve(factoryRoot, 'projects/oneCycAc/samples/AKWF_0906.wav'))
  return zipSync({
    'projects/bt9-midi/lgptsav.dat': new Uint8Array(midiProject),
    'projects/oneCycAc/lgptsav.dat': new Uint8Array(sampleProject),
    'projects/oneCycAc/samples/AKWF_0906.wav': new Uint8Array(sample),
  }, { level: 0 })
}

async function deleteIdbfsDatabase(page) {
  await page.goto('/oracle.html')
  await page.evaluate(async () => {
    await new Promise((resolveDelete, reject) => {
      const request = indexedDB.deleteDatabase('/data')
      request.onsuccess = () => resolveDelete()
      request.onerror = () => reject(request.error)
      request.onblocked = () => reject(new Error('IDBFS database remained open during factory-content cleanup'))
    })
  })
}

async function restartRuntime(page) {
  const ready = page.locator('[data-runtime-state="ready"]')
  await restartWorkbench(page)
  // Waiting only for "ready" can resolve against the old runtime before its
  // asynchronous shutdown begins. The hidden edge is the restart fence.
  await ready.waitFor({ state: 'hidden', timeout: 10_000 })
  await expect(ready).toBeVisible({ timeout: 20_000 })
}

async function modelSnapshot(page) {
  return page.evaluate(() => globalThis.__picoTrackerViewsTest.modelSnapshot())
}

async function expectModel(page, expected) {
  await expect.poll(() => modelSnapshot(page), { timeout: 10_000 }).toMatchObject(expected)
}

async function inputGeneration(page) {
  return Number(await page.locator('#picotracker-canvas').getAttribute('data-action-generation'))
}

async function tap(page, key) {
  const before = await inputGeneration(page)
  await page.keyboard.down(key)
  await expect.poll(() => inputGeneration(page)).toBeGreaterThanOrEqual(before + 1)
  await page.keyboard.up(key)
  await expect.poll(() => inputGeneration(page)).toBeGreaterThanOrEqual(before + 2)
}

async function chord(page, modifier, key) {
  const before = await inputGeneration(page)
  await page.keyboard.down(modifier)
  await expect.poll(() => inputGeneration(page)).toBeGreaterThanOrEqual(before + 1)
  await page.keyboard.down(key)
  await expect.poll(() => inputGeneration(page)).toBeGreaterThanOrEqual(before + 2)
  await page.keyboard.up(key)
  await expect.poll(() => inputGeneration(page)).toBeGreaterThanOrEqual(before + 3)
  await page.keyboard.up(modifier)
  await expect.poll(() => inputGeneration(page)).toBeGreaterThanOrEqual(before + 4)
}

async function readVirtualFile(page, path) {
  return page.evaluate((requestedPath) => globalThis.__picoTrackerStorageTest.read(requestedPath), path)
}

async function virtualFileExists(page, path) {
  return page.evaluate(
    (requestedPath) => globalThis.__picoTrackerStorageTest.exists(requestedPath),
    path,
  )
}

async function storageSnapshot(page) {
  return page.evaluate(() => globalThis.__picoTrackerStorageTest.snapshot())
}

async function awaitStorageDurable(page, generation) {
  return page.evaluate(
    (targetGeneration) => globalThis.__picoTrackerStorageTest.awaitDurable(targetGeneration),
    generation,
  )
}

async function expectPersistedFactoryState(page, editedHash) {
  expect(Buffer.from(await readVirtualFile(page, '/data/.current')).toString('utf8')).toBe('oneCycAc')

  const projectText = Buffer.from(await readVirtualFile(page, factoryProjectPath)).toString('utf8')
  expect(projectText).toContain('NAME="tempo" VALUE="164"')
  expect(projectText).toContain(`NAME="sample" VALUE="${editableSampleName}"`)

  expect(sha256(Buffer.from(await readVirtualFile(page, factoryProjectSamplePath))))
    .toBe(fixtureFiles[2].sha256)
  expect(sha256(Buffer.from(await readVirtualFile(page, editableLibrarySamplePath))))
    .toBe(fixtureFiles[3].sha256)
  expect(sha256(Buffer.from(await readVirtualFile(page, editableProjectSamplePath))))
    .toBe(editedHash)
}

test('PicoTracker 2.0 bt9 MIDI project loads from another project without a samples directory', async ({ page }) => {
  test.setTimeout(60_000)
  await deleteIdbfsDatabase(page)

  await page.goto('/?storage-test=1&views-test=1&inputDiagnostics=1&audio=disabled')
  await expect(page.locator('[data-runtime-state="ready"]')).toBeVisible({ timeout: 20_000 })
  await page.getByRole('button', { name: 'Files', exact: true }).click()
  await page.getByRole('button', { name: 'Restore ZIP' }).click()
  await page.locator('input[accept*="zip"]').setInputFiles({
    name: 'picotracker-factory-bt9-midi.zip',
    mimeType: 'application/zip',
    buffer: Buffer.from(loadLegacyMidiFixture()),
  })
  await expect(page.getByText(/Restore preview.*3 files/)).toBeVisible()
  await page.getByRole('button', { name: 'Overwrite conflicts' }).click()
  await expect(page.getByText('Restored 3 files', { exact: true })).toBeVisible()

  await restartRuntime(page)
  await expectModel(page, {
    projectName: '.untitled',
    playerRunning: false,
  })

  await page.getByRole('button', { name: 'Device', exact: true }).click()
  await chord(page, 'x', 'w')
  await tap(page, 'd')
  await tap(page, 'k')
  await tap(page, 'k')
  await expectModel(page, {
    projectName: 'bt9-midi',
    tempo: 86,
    playerRunning: false,
  })

  // Switching again is a separate transaction path from restoring oneCycAc
  // as the startup project. Exercise that exact browser workflow too.
  await chord(page, 'x', 'w')
  await tap(page, 'd')
  await tap(page, 'k')
  await tap(page, 's')
  await tap(page, 'k')
  await expectModel(page, {
    projectName: 'oneCycAc',
    tempo: 163,
    sampleCount: 1,
    playerRunning: false,
  })

  // OPTION+RIGHT selects LIVE and ENTER+PLAY starts the highlighted track
  // immediately. The sequencer must still run with browser audio disabled so
  // the device UI can show playback cursors and the bottom-bar played note.
  await chord(page, 'j', 'd')
  await chord(page, 'k', 'c')
  await expectModel(page, { playerRunning: true })
})

test('real oneCycAc project imports, trims, plays, and survives reload plus runtime restart', async ({ page }) => {
  test.setTimeout(workletMode ? 150_000 : 120_000)
  const fixtureZip = loadFactoryFixture()
  await deleteIdbfsDatabase(page)

  const query = new URLSearchParams({
    'storage-test': '1',
    'views-test': '1',
    inputDiagnostics: '1',
  })
  if (workletMode) query.set('audio', 'worklet')
  else query.set('audio', 'disabled')
  await page.goto(`/?${query}`)
  await expect(page.locator('[data-runtime-state="ready"]')).toBeVisible({ timeout: 20_000 })
  await expect(page.locator('[data-storage-state="ready"]')).toBeVisible()

  await page.getByRole('button', { name: 'Files', exact: true }).click()
  await page.getByRole('button', { name: 'Restore ZIP' }).click()
  await page.locator('input[accept*="zip"]').setInputFiles({
    name: 'picotracker-factory-oneCycAc.zip',
    mimeType: 'application/zip',
    buffer: Buffer.from(fixtureZip),
  })
  await expect(page.getByText(/Restore preview.*4 files.*1 conflicts/)).toBeVisible()
  await page.getByRole('button', { name: 'Overwrite conflicts' }).click()
  await expect(page.getByText('Restored 4 files', { exact: true })).toBeVisible()

  await restartRuntime(page)
  await expectModel(page, {
    projectName: 'oneCycAc',
    tempo: 163,
    sampleCount: 1,
    playerRunning: false,
  })

  await page.getByRole('button', { name: 'Device', exact: true }).click()
  const audioDiagnostics = page.locator('.audio-diagnostics')
  if (workletMode) {
    await page.getByRole('dialog', { name: 'Enable sound' }).getByRole('button', { name: 'Enable sound' }).click()
    await expect(page.locator('[data-audio-state="running"]')).toBeVisible({ timeout: 12_000 })
    await expect(audioDiagnostics).toHaveAttribute('data-audio-capability', 'available')
  } else {
    // The stable factory-content gate proves the real C++ Player state machine.
    // Actual browser output is covered only by the opt-in AudioWorklet mode,
    // because Chromium can currently hang during worklet bootstrap in CI.
    test.info().annotations.push({
      type: 'audio-evidence',
      description: 'C++ Player start/stop only; set PICOTRACKER_AUDIO_E2E=1 for AudioWorklet callbacks',
    })
    await expect(audioDiagnostics).toHaveAttribute('data-audio-capability', 'unavailable')
    await expect(audioDiagnostics).toHaveAttribute('data-audio-capability-reason', /Audio disabled/)
  }

  await tap(page, 'c')
  await expectModel(page, { playerRunning: true })
  if (workletMode) {
    await expect.poll(async () => (await modelSnapshot(page)).masterLevel, { timeout: 10_000 }).not.toBe(0)
    const callbacks = page.locator('[data-audio-worklet-callbacks]')
    const beforeCallbacks = Number(await callbacks.getAttribute('data-audio-worklet-callbacks'))
    await expect.poll(() => callbacks.getAttribute('data-audio-worklet-callbacks')).not.toBe(String(beforeCallbacks))
  }
  await tap(page, 'c')
  await expectModel(page, { playerRunning: false })

  // Exercise the real fixed Node controls instead of a diagnostic view jump:
  // SHIFT+UP opens Project, then ENTER+RIGHT changes tempo. Project's name
  // actions are NEW, LOAD, SAVE, RENAME, so two RIGHT presses select SAVE.
  await chord(page, 'x', 'w')
  await tap(page, 's')
  await chord(page, 'k', 'd')
  await expectModel(page, { tempo: 164 })
  await tap(page, 'w')
  await tap(page, 'd')
  await tap(page, 'd')

  const beforeSave = await storageSnapshot(page)
  await tap(page, 'k')

  await expect.poll(
    async () => (await storageSnapshot(page)).mutationGeneration,
    { timeout: 10_000, message: 'Project Save did not report its C++ filesystem mutation' },
  ).toBeGreaterThan(beforeSave.mutationGeneration)
  const saveMutation = await storageSnapshot(page)
  expect(saveMutation.reason).toBe('mutation')
  const durableGeneration = await awaitStorageDurable(page, saveMutation.mutationGeneration)
  expect(durableGeneration).toBeGreaterThanOrEqual(saveMutation.mutationGeneration)
  await expect.poll(
    async () => (await storageSnapshot(page)).durableGeneration,
    { timeout: 10_000 },
  ).toBeGreaterThanOrEqual(saveMutation.mutationGeneration)

  await expect.poll(async () => {
    const bytes = await readVirtualFile(page, factoryProjectPath)
    return Buffer.from(bytes).toString('utf8')
  }, { timeout: 10_000 }).toContain('NAME="tempo" VALUE="164"')
  expect(Buffer.from(await readVirtualFile(page, '/data/.current')).toString('utf8')).toBe('oneCycAc')
  expect(sha256(Buffer.from(await readVirtualFile(
    page,
    factoryProjectSamplePath,
  )))).toBe(fixtureFiles[2].sha256)

  // Focus remains on Project/Save. Walk the real Node field UI to Sample Pool,
  // switch from the project pool to /data/samples with SHIFT+OPTION, and import the
  // renamed, known-good factory WAV. Preview is meaningful only when the
  // AudioWorklet gate is enabled; the default gate declares audio unavailable.
  for (let index = 0; index < 5; index += 1) await tap(page, 's')
  await tap(page, 'k')
  await chord(page, 'x', 'j')
  await tap(page, 's') // skip /data/samples' parent-directory entry
  if (workletMode) await tap(page, 'c')
  await tap(page, 'k')
  await expectModel(page, { sampleCount: 2 })
  await expect.poll(
    () => page.evaluate(
      (path) => globalThis.__picoTrackerStorageTest.exists(path),
      editableProjectSamplePath,
    ),
    { timeout: 10_000 },
  ).toBe(true)

  const importedBytes = Buffer.from(await readVirtualFile(page, editableProjectSamplePath))
  expect(importedBytes.toString('ascii', 0, 4)).toBe('RIFF')
  expect(importedBytes.readUInt32LE(40)).toBe(1_200)
  const expectedEditedBytes = expectedOneFrameTrim(importedBytes)
  const expectedEditedHash = sha256(expectedEditedBytes)

  // Return to the project pool, choose the newly imported (sorted-last) WAV,
  // and enter SampleEditor through its real Edit action.
  await chord(page, 'x', 'j')
  await tap(page, 's')
  await tap(page, 'k')

  // waveform -> start; select the least-significant hex digit, then ENTER+UP
  // changes the start frame from 0 to 1. Move through end and the default Trim
  // operation to Apply, then select Yes in the modal.
  await tap(page, 's')
  for (let digit = 0; digit < 6; digit += 1) await tap(page, 'd')
  await chord(page, 'k', 'w')
  await tap(page, 's')
  await tap(page, 's')
  await tap(page, 's')
  const beforeApply = await storageSnapshot(page)
  await tap(page, 'k')
  await tap(page, 'a')
  await tap(page, 'k')
  await expect.poll(
    async () => (await storageSnapshot(page)).mutationGeneration,
    { timeout: 10_000, message: 'SampleEditor Apply did not mutate its working WAV' },
  ).toBeGreaterThan(beforeApply.mutationGeneration)
  const appliedMutation = await storageSnapshot(page)
  await awaitStorageDurable(page, appliedMutation.mutationGeneration)
  // APPLY mutates only the hidden transaction copy. The authoritative project
  // sample is byte-identical until SAVE promotes the validated replacement.
  expect(Buffer.from(await readVirtualFile(page, editableProjectSamplePath))).toEqual(importedBytes)
  expect(await virtualFileExists(page, editableWorkingSamplePath)).toBe(true)
  expect(Buffer.from(await readVirtualFile(page, editableWorkingSamplePath))).toEqual(expectedEditedBytes)
  expect(await virtualFileExists(page, editableBackupSamplePath)).toBe(false)

  // Apply remains focused after the field list rebuild. DOWN selects Save,
  // which atomically commits the working WAV in place.
  await tap(page, 's')
  const beforeSampleSave = await storageSnapshot(page)
  await tap(page, 'k')
  await expect.poll(
    async () => (await storageSnapshot(page)).mutationGeneration,
    { timeout: 10_000, message: 'SampleEditor Save did not report its filesystem mutation' },
  ).toBeGreaterThan(beforeSampleSave.mutationGeneration)
  const sampleSaveMutation = await storageSnapshot(page)
  const sampleDurableGeneration = await awaitStorageDurable(
    page,
    sampleSaveMutation.mutationGeneration,
  )
  expect(sampleDurableGeneration).toBeGreaterThanOrEqual(sampleSaveMutation.mutationGeneration)

  const editedBytes = Buffer.from(await readVirtualFile(page, editableProjectSamplePath))
  const editedHash = sha256(editedBytes)
  expect(editedBytes).toEqual(expectedEditedBytes)
  expect(editedHash).toBe(expectedEditedHash)
  expect(await virtualFileExists(page, editableWorkingSamplePath)).toBe(false)
  expect(await virtualFileExists(page, editableBackupSamplePath)).toBe(false)
  expect(editedHash).not.toBe(sha256(importedBytes))
  expect(editedBytes.readUInt32LE(40)).toBe(importedBytes.readUInt32LE(40) - 2)

  // Leave Import with SHIFT+LEFT, return from Sample Pool to Project/Save, and
  // persist the model after assigning the new sample to the current instrument.
  await chord(page, 'x', 'a')
  for (let index = 0; index < 5; index += 1) await tap(page, 'w')
  const beforeImportedProjectSave = await storageSnapshot(page)
  await tap(page, 'k')
  await expect.poll(
    async () => (await storageSnapshot(page)).mutationGeneration,
    { timeout: 10_000, message: 'Imported project Save did not report a mutation' },
  ).toBeGreaterThan(beforeImportedProjectSave.mutationGeneration)
  const importedProjectSave = await storageSnapshot(page)
  await awaitStorageDurable(page, importedProjectSave.mutationGeneration)

  await page.reload()
  await expect(page.locator('[data-runtime-state="ready"]')).toBeVisible({ timeout: 20_000 })
  await expectModel(page, { projectName: 'oneCycAc', tempo: 164, sampleCount: 2 })
  await expectPersistedFactoryState(page, editedHash)

  await restartRuntime(page)
  await expectModel(page, { projectName: 'oneCycAc', tempo: 164, sampleCount: 2 })
  await expectPersistedFactoryState(page, editedHash)

  // Playback remains operational after both persistence boundaries.
  await tap(page, 'c')
  await expectModel(page, { playerRunning: true })
  if (workletMode) {
    await expect.poll(async () => (await modelSnapshot(page)).masterLevel, { timeout: 10_000 }).not.toBe(0)
  }
  await tap(page, 'c')
  await expectModel(page, { playerRunning: false })

  // Re-open the persisted project pool with Node controls. In AudioWorklet
  // mode, preview the edited WAV once more; a bad header would open a modal.
  await chord(page, 'x', 'w')
  for (let index = 0; index < 6; index += 1) await tap(page, 's')
  await tap(page, 'k')
  await tap(page, 's')
  if (workletMode) await tap(page, 'c')

  await stopWorkbench(page)
  await expect(page.locator('[data-runtime-state="idle"]')).toBeVisible({ timeout: 10_000 })
})
