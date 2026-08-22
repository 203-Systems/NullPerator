function requireExport(module, name) {
  const exported = module?.[name]
  if (typeof exported !== 'function') {
    throw new TypeError(`WASM module does not export ${name}`)
  }
  return exported
}

export function createInputBridge(module) {
  const setAction = requireExport(module, '_PicoTracker_Wasm_SetAction')
  const releaseAll = requireExport(module, '_PicoTracker_Wasm_ReleaseAllActions')
  const getActionMask = requireExport(module, '_PicoTracker_Wasm_GetActionMask')
  const getActionGeneration = requireExport(module, '_PicoTracker_Wasm_GetActionGeneration')
  const getLastAction = requireExport(module, '_PicoTracker_Wasm_GetLastAction')

  return Object.freeze({
    pressAction(action) {
      setAction(action, true)
    },
    releaseAction(action) {
      setAction(action, false)
    },
    releaseAllActions() {
      releaseAll()
    },
    getActionMask,
    getActionGeneration,
    getLastAction,
  })
}

export function pressAction(module, action) {
  requireExport(module, '_PicoTracker_Wasm_SetAction')(action, true)
}

export function releaseAction(module, action) {
  requireExport(module, '_PicoTracker_Wasm_SetAction')(action, false)
}

export function releaseAllActions(module) {
  requireExport(module, '_PicoTracker_Wasm_ReleaseAllActions')()
}
