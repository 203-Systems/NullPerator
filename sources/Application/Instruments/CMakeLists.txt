add_library(application_instruments
  CommandList.cpp
  Filters.cpp
  I_Instrument.cpp
  InstrumentBank.cpp
  InstrumentNameVariable.cpp
  MidiInstrument.cpp
  OpalInstrument.cpp
  NoneInstrument.cpp
  SRPUpdaters.cpp
  SampleInstrument.cpp
  SamplePool.cpp
  SamplePoolLoading.cpp
  SampleVariable.cpp
  SIDInstrument.cpp
  WavFile.cpp
  WavFileWriter.cpp
  WavHeader.cpp
)

# Keep the measured sampler hot path at O2. On ESP32-S3 hardware, forcing this
# translation unit to O3 was consistently slower, most likely because its
# larger code footprint adds instruction-cache pressure.
if(Node)
  set_source_files_properties(SampleInstrument.cpp PROPERTIES
    COMPILE_OPTIONS "-O2"
  )
endif()

target_link_libraries(application_instruments PUBLIC
  foundation_services
  system_io
  crsid
  opal
  SampleRate::samplerate
  etl
  application_utils
)

target_include_directories(application_instruments PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

include_directories(${PROJECT_SOURCE_DIR})
