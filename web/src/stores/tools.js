export const DEVICE_TOOLS = Object.freeze([
  { id: 'Files', label: 'Files' },
  { id: 'MIDI', label: 'MIDI' },
  { id: 'Logs', label: 'Logs' },
  { id: 'Trace', label: 'Trace' },
])

export function toggleTool(openTools, toolId) {
  return openTools.includes(toolId)
    ? openTools.filter((id) => id !== toolId)
    : [...openTools, toolId].slice(-2)
}
