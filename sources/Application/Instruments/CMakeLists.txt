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
  SoundSource.cpp
  WavFile.cpp
  WavFileWriter.cpp
  WavHeader.cpp
)

# Keep the more aggressive optimization local to the sampler hot path. Global
# -O3 increases code size and can put extra pressure on the ESP32-S3 instruction
# cache, while this translation unit contains the measured render bottleneck.
if(Node)
  set_source_files_properties(SampleInstrument.cpp PROPERTIES
    COMPILE_OPTIONS "-O3"
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
  profiler
)

target_include_directories(application_instruments PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

include_directories(${PROJECT_SOURCE_DIR})
