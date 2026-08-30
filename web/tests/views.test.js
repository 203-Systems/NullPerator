import { describe, expect, it, vi } from 'vitest'
import { createViewDiagnostics, MODAL_NAMES, VIEW_NAMES } from '../src/handles/views.js'

function moduleWith(overrides = {}) {
  return {
    _PicoTracker_Wasm_RequestDiagnosticView: vi.fn(),
    _PicoTracker_Wasm_GetDiagnosticView: vi.fn(),
    _PicoTracker_Wasm_GetDiagnosticViewGeneration: vi.fn(),
    _PicoTracker_Wasm_GetDiagnosticInputGeneration: vi.fn(),
    _PicoTracker_Wasm_RequestDiagnosticModal: vi.fn(),
    _PicoTracker_Wasm_GetDiagnosticModal: vi.fn(),
    _PicoTracker_Wasm_GetDiagnosticModalGeneration: vi.fn(),
    ...overrides,
  }
}

describe('view acceptance diagnostics', () => {
  it('keeps historical view ids stable, appends Font, and forwards native counters', () => {
    const request = vi.fn()
    const handle = createViewDiagnostics(moduleWith({
      _PicoTracker_Wasm_RequestDiagnosticView: request,
      _PicoTracker_Wasm_GetDiagnosticView: () => 7,
      _PicoTracker_Wasm_GetDiagnosticViewGeneration: () => 12,
      _PicoTracker_Wasm_GetDiagnosticInputGeneration: () => 34,
    }))
    expect(VIEW_NAMES).toHaveLength(20)
    expect(VIEW_NAMES.slice(0, 19)).toEqual([
      'Song', 'Chain', 'Phrase', 'Project', 'Device', 'Instrument',
      'Phrase Table', 'Instrument Table', 'Groove', 'Mixer', 'Sample Import',
      'Instrument Import', 'Select Project', 'Theme', 'Select Theme',
      'Theme Import', 'Sample Editor', 'Sample Slices', 'Record',
    ])
    expect(VIEW_NAMES.at(-1)).toBe('Font')
    handle.request(18)
    handle.request(19)
    expect(request.mock.calls).toEqual([[18], [19]])
    expect([handle.current(), handle.generation(), handle.inputGeneration()]).toEqual([7, 12, 34])
  })

  it('rejects invalid view ids before crossing the WASM ABI', () => {
    const request = vi.fn()
    const handle = createViewDiagnostics(moduleWith({
      _PicoTracker_Wasm_RequestDiagnosticView: request,
    }))
    for (const value of [-1, 20, 1.5, '1']) expect(() => handle.request(value)).toThrow(RangeError)
    expect(request).not.toHaveBeenCalled()
  })

  it('keeps all modal UI classes ordered and validates the modal ABI', () => {
    const requestModal = vi.fn()
    const handle = createViewDiagnostics(moduleWith({
      _PicoTracker_Wasm_RequestDiagnosticModal: requestModal,
      _PicoTracker_Wasm_GetDiagnosticModal: () => 3,
      _PicoTracker_Wasm_GetDiagnosticModalGeneration: () => 9,
    }))
    expect(MODAL_NAMES).toEqual([
      'Message Box', 'Text Input', 'Render Progress', 'Full Screen Box',
      'Rename',
    ])
    handle.openModal(4)
    handle.closeModal()
    expect(requestModal.mock.calls).toEqual([[4], [5]])
    expect([handle.currentModal(), handle.modalGeneration()]).toEqual([3, 9])
    for (const value of [-1, 5, 1.5, '1']) {
      expect(() => handle.openModal(value)).toThrow(RangeError)
    }
  })

})
