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
  it('keeps the authoritative 19 ViewType order and forwards native counters', () => {
    const request = vi.fn()
    const handle = createViewDiagnostics(moduleWith({
      _PicoTracker_Wasm_RequestDiagnosticView: request,
      _PicoTracker_Wasm_GetDiagnosticView: () => 7,
      _PicoTracker_Wasm_GetDiagnosticViewGeneration: () => 12,
      _PicoTracker_Wasm_GetDiagnosticInputGeneration: () => 34,
    }))
    expect(VIEW_NAMES).toHaveLength(19)
    expect(VIEW_NAMES[0]).toBe('Song')
    expect(VIEW_NAMES.at(-1)).toBe('Record')
    handle.request(18)
    expect(request).toHaveBeenCalledWith(18)
    expect([handle.current(), handle.generation(), handle.inputGeneration()]).toEqual([7, 12, 34])
  })

  it('rejects invalid view ids before crossing the WASM ABI', () => {
    const request = vi.fn()
    const handle = createViewDiagnostics(moduleWith({
      _PicoTracker_Wasm_RequestDiagnosticView: request,
    }))
    for (const value of [-1, 19, 1.5, '1']) expect(() => handle.request(value)).toThrow(RangeError)
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
    ])
    handle.openModal(3)
    handle.closeModal()
    expect(requestModal.mock.calls).toEqual([[3], [4]])
    expect([handle.currentModal(), handle.modalGeneration()]).toEqual([3, 9])
    for (const value of [-1, 4, 1.5, '1']) {
      expect(() => handle.openModal(value)).toThrow(RangeError)
    }
  })

})
