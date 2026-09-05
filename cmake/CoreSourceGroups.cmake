# Evaluate in each caller's directory scope. A global include guard would leave
# later sibling targets without their source lists.
set(_picotracker_sources "${CMAKE_CURRENT_LIST_DIR}/../sources")
set(PICOTRACKER_CONFIG_SOURCES
  "${_picotracker_sources}/Application/Model/Config.cpp"
  "${_picotracker_sources}/Application/Model/ThemeDocument.cpp"
  "${_picotracker_sources}/Application/Model/ThemePersistence.cpp")
set(PICOTRACKER_PERSISTENCY_SERVICE_SOURCES
  "${_picotracker_sources}/Application/Persistency/PersistencyService.cpp"
  "${_picotracker_sources}/Application/Persistency/StagingPersistence.cpp"
  "${_picotracker_sources}/Application/Persistency/InstrumentPersistence.cpp")
