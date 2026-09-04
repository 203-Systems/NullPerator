export const PRIMARY_SECTIONS = Object.freeze(['Device', 'Files', 'MIDI'])
export const UTILITY_SECTIONS = Object.freeze(['Settings'])
export const USER_SECTIONS = Object.freeze([...PRIMARY_SECTIONS, ...UTILITY_SECTIONS])
export const DEVELOPER_SECTIONS = Object.freeze(['Logs', 'Trace'])
export const SECTION_LABELS = Object.freeze({ Device: 'Tracker' })

export function sectionLabel(section) {
  return SECTION_LABELS[section] ?? section
}

export function visibleSections(developerMode) {
  return developerMode
    ? [...PRIMARY_SECTIONS, ...DEVELOPER_SECTIONS, ...UTILITY_SECTIONS]
    : [...USER_SECTIONS]
}

export function isDeveloperSection(section) {
  return DEVELOPER_SECTIONS.includes(section)
}
