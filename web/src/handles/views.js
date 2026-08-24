import { readApplicationSnapshot } from './applicationSnapshot.js'

export const VIEW_NAMES = Object.freeze([
  'Song', 'Chain', 'Phrase', 'Project', 'Device', 'Instrument',
  'Phrase Table', 'Instrument Table', 'Groove', 'Mixer', 'Sample Import',
  'Instrument Import', 'Select Project', 'Theme', 'Select Theme',
  'Theme Import', 'Sample Editor', 'Sample Slices', 'Record',
])

export const MODAL_NAMES = Object.freeze([
  'Message Box', 'Text Input', 'Render Progress', 'Full Screen Box',
  'Rename',
])

function requireExport(module, name) {
  const value = module?.[name]
  if (typeof value !== 'function') throw new TypeError(`WASM module does not export ${name}`)
  return value
}

export function createViewDiagnostics(module) {
  const request = requireExport(module, '_PicoTracker_Wasm_RequestDiagnosticView')
  const current = requireExport(module, '_PicoTracker_Wasm_GetDiagnosticView')
  const generation = requireExport(module, '_PicoTracker_Wasm_GetDiagnosticViewGeneration')
  const inputGeneration = requireExport(module, '_PicoTracker_Wasm_GetDiagnosticInputGeneration')
  const requestModal = requireExport(module, '_PicoTracker_Wasm_RequestDiagnosticModal')
  const currentModal = requireExport(module, '_PicoTracker_Wasm_GetDiagnosticModal')
  const modalGeneration = requireExport(module, '_PicoTracker_Wasm_GetDiagnosticModalGeneration')
  return Object.freeze({
    names: VIEW_NAMES,
    modalNames: MODAL_NAMES,
    request(viewType) {
      if (!Number.isInteger(viewType) || viewType < 0 || viewType >= VIEW_NAMES.length) {
        throw new RangeError(`Unknown PicoTracker view ${viewType}`)
      }
      request(viewType)
    },
    current,
    generation,
    inputGeneration,
    openModal(modalType) {
      if (!Number.isInteger(modalType) || modalType < 0 || modalType >= MODAL_NAMES.length) {
        throw new RangeError(`Unknown PicoTracker modal ${modalType}`)
      }
      requestModal(modalType)
    },
    closeModal() { requestModal(MODAL_NAMES.length) },
    currentModal: () => currentModal() >>> 0,
    modalGeneration,
    modelSnapshot: () => readApplicationSnapshot(module),
  })
}
