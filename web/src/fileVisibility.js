const INTERNAL_ROOT_NAMES = new Set([
  '.config.xml',
  '.current',
  '.current.tmp',
  '.current.bak',
  '.current.bak.tmp',
  '.picotracker-untitled-session-pending',
  '.picotracker-untitled-session-pending.tmp',
  '.picotracker-untitled-session-commit',
  '.picotracker-untitled-session-commit.tmp',
  '.picotracker-untitled-session-purge',
  '.picotracker-untitled-session-purge.tmp',
])

const INTERNAL_PROJECT_DIRECTORY_NAMES = new Set([
  '.untitled',
  '.untitled.session-backup',
])

const PROJECT_DATA_JOURNAL_NAMES = new Set([
  'lgptsav.tmp',
  'lgptsav.bak',
  'autosave.tmp',
  'autosave.bak',
])

const COPY_TEMP_PREFIX = '.picotracker-copy-temp-'
const COPY_BACKUP_PREFIX = '.picotracker-copy-backup-'
const SAVE_AS_STAGE_PREFIX = '.picotracker-saveas-stage.'
const SAVE_AS_BACKUP_PREFIX = '.picotracker-saveas-backup.'
const SAMPLE_DELETE_PREFIX = '.ui2-sample-delete.'
const SAMPLE_DELETE_SUFFIX = '.tmp'

// Keep these byte limits in lockstep with PersistenceConstants.h. The
// firmware uses strlen(), so JavaScript must count encoded UTF-8 bytes rather
// than UTF-16 code units when recognizing a journal it could have created.
const MAX_PROJECT_NAME_BYTES = 16
const MAX_INSTRUMENT_NAME_BYTES = 20
const MAX_SAMPLE_NAME_BYTES = 24
const MAX_THEME_NAME_BYTES = 16
const UTF8_ENCODER = new TextEncoder()

const byteLength = (value) => UTF8_ENCODER.encode(value).byteLength
const isFlatLeaf = (name) => typeof name === 'string'
  && name.length > 0
  && name !== '.'
  && name !== '..'
  && !name.includes('\0')
  && !name.includes('/')
  && !name.includes('\\')

const isBoundedFlatLeaf = (name, maxBytes) => isFlatLeaf(name)
  && byteLength(name) <= maxBytes

const isProjectSamplesDirectory = (directory) =>
  /^\/data\/projects\/[^/]+\/samples$/.test(directory)

const isEditableSamplesDirectory = (directory) =>
  isProjectSamplesDirectory(directory)
  || directory === '/data/samples'
  || directory.startsWith('/data/samples/')
  || directory === '/data/recordings'

const isCopyJournal = (name) => {
  const prefix = name.startsWith(COPY_TEMP_PREFIX)
    ? COPY_TEMP_PREFIX
    : name.startsWith(COPY_BACKUP_PREFIX)
      ? COPY_BACKUP_PREFIX
      : null
  if (prefix === null) return false
  const encodedLeaf = name.slice(prefix.length)
  return encodedLeaf.length > 0
    && encodedLeaf.length % 2 === 0
    && /^[0-9A-F]+$/.test(encodedLeaf)
}

const isInternalProjectName = (name) => INTERNAL_PROJECT_DIRECTORY_NAMES.has(name)
  || name.startsWith(SAVE_AS_STAGE_PREFIX)
  || name.startsWith(SAVE_AS_BACKUP_PREFIX)

const isUserProjectName = (name) => isBoundedFlatLeaf(name, MAX_PROJECT_NAME_BYTES)
  && !isInternalProjectName(name)

const isSaveAsJournal = (name) => {
  const prefix = name.startsWith(SAVE_AS_STAGE_PREFIX)
    ? SAVE_AS_STAGE_PREFIX
    : name.startsWith(SAVE_AS_BACKUP_PREFIX)
      ? SAVE_AS_BACKUP_PREFIX
      : null
  return prefix !== null && isUserProjectName(name.slice(prefix.length))
}

const isSampleEditorJournal = (name) => {
  if (name.length < 4 || name[0] !== '.') return false
  const suffix = name.slice(-3)
  if (suffix[0] !== '.' || !'wob'.includes(suffix[1]) || suffix[2] < '0' || suffix[2] > '7') return false

  // Decode the journal leaf exactly as SampleEditorFileJournal does. The
  // case nibble only affects the WAV extension and does not change its size.
  const source = `${name.slice(1, -3)}.wav`
  return isBoundedFlatLeaf(source, MAX_SAMPLE_NAME_BYTES)
    && !source.startsWith(COPY_TEMP_PREFIX)
    && !source.startsWith(COPY_BACKUP_PREFIX)
}

const isSampleDeleteJournal = (name) => {
  if (!name.startsWith(SAMPLE_DELETE_PREFIX) || !name.endsWith(SAMPLE_DELETE_SUFFIX)) return false
  const sampleName = name.slice(SAMPLE_DELETE_PREFIX.length, -SAMPLE_DELETE_SUFFIX.length)
  return isBoundedFlatLeaf(sampleName, MAX_SAMPLE_NAME_BYTES)
    && !sampleName.startsWith(COPY_TEMP_PREFIX)
    && !sampleName.startsWith(COPY_BACKUP_PREFIX)
}

const isInstrumentExportJournal = (name) => {
  const suffix = name.slice(-4).toLowerCase()
  if (suffix !== '.tmp' && suffix !== '.bak') return false
  const stem = name.slice(0, -4)
  return stem[0] !== '.' && isBoundedFlatLeaf(stem, MAX_INSTRUMENT_NAME_BYTES)
}

const isThemeExportJournal = (name) => {
  const suffix = name.endsWith('.npt.tmp')
    ? '.npt.tmp'
    : name.endsWith('.npt.bak')
      ? '.npt.bak'
      : null
  if (suffix === null || name[0] !== '.') return false
  return isBoundedFlatLeaf(name.slice(1, -suffix.length), MAX_THEME_NAME_BYTES)
}

// Hide an entry only when its path, complete transaction grammar, and kind all
// agree with a firmware-owned artifact. In particular, Save As and untitled
// journals are directories; all other journal families below are files.
export function isInternalEntry(directory, entry) {
  if (!entry || typeof entry.name !== 'string') return false
  const { kind, name } = entry

  if (kind === 'file' && directory === '/data' && INTERNAL_ROOT_NAMES.has(name)) return true

  if (directory === '/data/projects') {
    if (kind === 'file' && name === '.load-rollback.dat') return true
    if (kind === 'directory' && (
      INTERNAL_PROJECT_DIRECTORY_NAMES.has(name)
      || isSaveAsJournal(name)
    )) return true
  }

  if (kind !== 'file') return false

  if (isCopyJournal(name)) return true

  if (/^\/data\/projects\/[^/]+$/.test(directory) && PROJECT_DATA_JOURNAL_NAMES.has(name)) return true

  if (isEditableSamplesDirectory(directory) && isSampleEditorJournal(name)) return true

  if (isProjectSamplesDirectory(directory) && isSampleDeleteJournal(name)) return true

  if (directory === '/data/instruments' && isInstrumentExportJournal(name)) return true

  return directory === '/data/themes' && isThemeExportJournal(name)
}
