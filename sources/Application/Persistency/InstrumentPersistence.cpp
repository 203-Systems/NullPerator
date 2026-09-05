/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2018 Discodirt
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "../Instruments/SamplePool.h"
#include "Foundation/Services/ServiceRegistry.h"
#include "PersistencyService.h"

#include "Foundation/Types/Types.h"
#include "InstrumentExportTransaction.h"
#include "InstrumentFileValidator.h"
#include "Persistent.h"
#include "ProjectFileJournal.h"
#include "System/Console/Trace.h"
#include "System/FileSystem/FileSystem.h"
#include <cstdio>
#include <cstring>

namespace {
bool IsSafeInstrumentFilename(const char *name) {
  if (name == nullptr)
    return false;
  const size_t length = std::strlen(name);
  return length > std::strlen(INSTRUMENT_FILE_EXTENSION) &&
         length <= MAX_INSTRUMENT_FILENAME_LENGTH &&
         std::strchr(name, '/') == nullptr &&
         std::strchr(name, '\\') == nullptr &&
         strcasecmp(name + length - std::strlen(INSTRUMENT_FILE_EXTENSION),
                    INSTRUMENT_FILE_EXTENSION) == 0;
}

bool ReadInstrumentEnvelope(const char *name, InstrumentType &type,
                            char *version, size_t versionCapacity) {
  type = IT_NONE;
  if (version != nullptr && versionCapacity != 0U)
    version[0] = '\0';
  if (!IsSafeInstrumentFilename(name))
    return false;

  PersistencyDocument doc;
  if (!doc.Load(name) || !doc.FirstChild() ||
      std::strcmp(doc.ElemName(), "INSTRUMENT") != 0) {
    return false;
  }

  bool hasType = false;
  bool hasVersion = false;
  bool attribute = doc.NextAttribute();
  while (attribute) {
    if (strcasecmp(doc.attrname_, "TYPE") == 0) {
      if (hasType)
        return false;
      hasType = true;
      for (int index = IT_NONE; index < IT_LAST; ++index) {
        if (strcasecmp(doc.attrval_, InstrumentTypeNames[index]) == 0) {
          type = static_cast<InstrumentType>(index);
          break;
        }
      }
    } else if (strcasecmp(doc.attrname_, "VERSION") == 0) {
      if (hasVersion)
        return false;
      hasVersion = true;
      if (version != nullptr && versionCapacity != 0U) {
        const size_t length = std::strlen(doc.attrval_);
        if (length >= versionCapacity)
          return false;
        std::memcpy(version, doc.attrval_, length + 1U);
      }
    }
    attribute = doc.NextAttribute();
  }
  return !doc.HadError() && hasType && type > IT_NONE && type < IT_LAST;
}

} // namespace

PersistencyResult PersistencyService::ExportInstrument(
    I_Instrument *instrument, etl::string<MAX_INSTRUMENT_NAME_LENGTH> name,
    bool overwrite) {
  FileSystem *fs = FileSystem::GetInstance();
  if (fs == nullptr || instrument == nullptr)
    return PERSIST_ERROR;

  char destination[MAX_PROJECT_SAMPLE_PATH_LENGTH]{};
  char temporary[MAX_PROJECT_SAMPLE_PATH_LENGTH]{};
  char backup[MAX_PROJECT_SAMPLE_PATH_LENGTH]{};
  if (!BuildInstrumentExportSiblingPaths(destination, temporary, backup,
                                         INSTRUMENTS_DIR, name.c_str())) {
    Trace::Error("PERSISTENCYSERVICE: Unsafe instrument export name");
    return PERSIST_ERROR;
  }

  const InstrumentExportTransactionResult result =
      ExportInstrumentFileAtomically(
          *fs, destination, temporary, backup, overwrite,
          [&](const char *path) {
            auto fp = fs->Open(path, "w");
            if (!fp) {
              Trace::Error(
                  "PERSISTENCYSERVICE: Could not open instrument temp: %s",
                  path);
              return false;
            }
            {
              tinyxml2::XMLPrinter printer(fp.get());
              instrument->Save(&printer);
            }
            const bool synced = fp->Sync() && fp->Error() == 0;
            I_File *rawFile = AcquireLegacyFileHandle_DO_NOT_USE(std::move(fp));
            const bool closed = CloseFile_DO_NOT_USE(rawFile);
            if (!synced || !closed) {
              Trace::Error(
                  "PERSISTENCYSERVICE: Failed to flush instrument temp: %s",
                  path);
            }
            return synced && closed;
          },
          [](const char *path) { return ValidateInstrumentFilePayload(path); });
  switch (result) {
  case InstrumentExportTransactionResult::Saved:
    return PERSIST_SAVED;
  case InstrumentExportTransactionResult::Exists:
    return PERSIST_EXISTS;
  case InstrumentExportTransactionResult::Error:
    return PERSIST_ERROR;
  }
  return PERSIST_ERROR;
}

bool PersistencyService::RecoverInstrumentExports() {
  FileSystem *fs = FileSystem::GetInstance();
  if (fs == nullptr || !fs->chdir(INSTRUMENTS_DIR))
    return false;

  const auto hasSuffix = [](const char *filename, const char *suffix,
                            size_t &stemLength) {
    if (filename == nullptr || suffix == nullptr)
      return false;
    const size_t length = std::strlen(filename);
    const size_t suffixLength = std::strlen(suffix);
    if (length <= suffixLength ||
        strcasecmp(filename + length - suffixLength, suffix) != 0) {
      return false;
    }
    stemLength = length - suffixLength;
    return stemLength <= MAX_INSTRUMENT_NAME_LENGTH && filename[0] != '.';
  };

  etl::vector<int, MAX_FILE_INDEX_SIZE> journals;
  if (!fs->listChecked(&journals, ".bak", false))
    return false;
  for (const int index : journals) {
    if (fs->getFileType(index) == PFT_DIR)
      continue;
    char filename[PFILENAME_SIZE]{};
    fs->getFileName(index, filename, sizeof(filename));
    size_t stemLength = 0U;
    if (!hasSuffix(filename, ".bak", stemLength))
      continue;
    char stem[MAX_INSTRUMENT_NAME_LENGTH + 1U]{};
    std::memcpy(stem, filename, stemLength);
    char destination[MAX_PROJECT_SAMPLE_PATH_LENGTH]{};
    char temporary[MAX_PROJECT_SAMPLE_PATH_LENGTH]{};
    char backup[MAX_PROJECT_SAMPLE_PATH_LENGTH]{};
    if (!BuildInstrumentExportSiblingPaths(destination, temporary, backup,
                                           INSTRUMENTS_DIR, stem)) {
      return false;
    }
    const int actualBackupLength = std::snprintf(
        backup, sizeof(backup), "%s/%s", INSTRUMENTS_DIR, filename);
    if (actualBackupLength <= 0 ||
        static_cast<size_t>(actualBackupLength) >= sizeof(backup) ||
        !RecoverInstrumentExportFile(
            *fs, destination, temporary, backup, [](const char *path) {
              return ValidateInstrumentFilePayload(path);
            })) {
      return false;
    }
  }

  // A .tmp without a sibling backup was never committed. It is safe to drop
  // after backup recovery and prevents interrupted first-time exports from
  // accumulating indefinitely.
  journals.clear();
  if (!fs->listChecked(&journals, ".tmp", false))
    return false;
  for (const int index : journals) {
    if (fs->getFileType(index) == PFT_DIR)
      continue;
    char filename[PFILENAME_SIZE]{};
    fs->getFileName(index, filename, sizeof(filename));
    size_t stemLength = 0U;
    if (!hasSuffix(filename, ".tmp", stemLength))
      continue;
    char path[MAX_PROJECT_SAMPLE_PATH_LENGTH]{};
    const int pathLength =
        std::snprintf(path, sizeof(path), "%s/%s", INSTRUMENTS_DIR, filename);
    if (pathLength <= 0 || static_cast<size_t>(pathLength) >= sizeof(path) ||
        (fs->exists(path) && !fs->DeleteFile(path))) {
      return false;
    }
  }
  return true;
}

bool RecoverInstrumentExportJournals() {
  PersistencyService *persistence = PersistencyService::GetInstance();
  return persistence != nullptr && persistence->RecoverInstrumentExports();
}

InstrumentType PersistencyService::DetectInstrumentType(const char *name) {
  FileSystem *fs = FileSystem::GetInstance();

  if (fs == nullptr || !fs->chdir(INSTRUMENTS_DIR)) {
    Trace::Error(
        "PERSISTENCYSERVICE: Could not change to instruments directory");
    return IT_NONE;
  }

  InstrumentType importedType = IT_NONE;
  if (!ReadInstrumentEnvelope(name, importedType, nullptr, 0U)) {
    Trace::Error("PERSISTENCYSERVICE: Invalid instrument envelope: %s",
                 name == nullptr ? "<null>" : name);
    return IT_NONE;
  }
  return importedType;
}

PersistencyResult PersistencyService::ImportInstrument(I_Instrument *instrument,
                                                       const char *name) {
  FileSystem *fs = FileSystem::GetInstance();

  if (fs == nullptr || !fs->chdir(INSTRUMENTS_DIR)) {
    Trace::Error(
        "PERSISTENCYSERVICE: Could not change to instruments directory");
    return PERSIST_ERROR;
  }

  InstrumentType importedType = IT_NONE;
  char versionInfo[64]{};
  if (!ReadInstrumentEnvelope(name, importedType, versionInfo,
                              sizeof(versionInfo))) {
    Trace::Error("PERSISTENCYSERVICE: Invalid instrument envelope: %s",
                 name == nullptr ? "<null>" : name);
    return PERSIST_ERROR;
  }

  // Log the complete version info if available
  if (versionInfo[0] != '\0') {
    Trace::Log("PERSISTENCYSERVICE",
               "Instrument created with firmware version: %s", versionInfo);
  }

  if (instrument == nullptr || importedType == IT_NONE ||
      importedType != instrument->GetType()) {
    Trace::Error("PERSISTENCYSERVICE",
                 "Instrument import type mismatch (target:%d file:%d)",
                 instrument == nullptr ? IT_NONE : instrument->GetType(),
                 importedType);
    return PERSIST_ERROR;
  }

  if (!ValidateInstrumentFilePayload(name)) {
    Trace::Error("PERSISTENCYSERVICE",
                 "Incomplete instrument payload in file: %s", name);
    return PERSIST_ERROR;
  }

  // Reload from the root because both type detection and structural
  // validation consume the forward-only parser.
  PersistencyDocument doc;
  if (!doc.Load(name) || !doc.FirstChild() ||
      std::strcmp(doc.ElemName(), "INSTRUMENT") != 0)
    return PERSIST_ERROR;

  // Restore the instrument content
  if (!instrument->Restore(&doc) || !doc.Finish()) {
    Trace::Error(
        "PERSISTENCYSERVICE: Failed to restore instrument from file: %s", name);
    return PERSIST_ERROR;
  }

  // Extract instrument name from filename (minus .pti extension)
  etl::string<MAX_INSTRUMENT_NAME_LENGTH> instrumentName;
  const char *dotPos = strrchr(name, '.');
  if (dotPos) {
    // Calculate the length of the name without extension
    size_t nameLength = dotPos - name;
    // Copy only up to MAX_INSTRUMENT_NAME_LENGTH characters
    nameLength = nameLength <= MAX_INSTRUMENT_NAME_LENGTH
                     ? nameLength
                     : MAX_INSTRUMENT_NAME_LENGTH;
    instrumentName.assign(name, nameLength);
  } else {
    // No extension found, use the whole name (up to MAX_INSTRUMENT_NAME_LENGTH)
    instrumentName.assign(name, strlen(name) <= MAX_INSTRUMENT_NAME_LENGTH
                                    ? strlen(name)
                                    : MAX_INSTRUMENT_NAME_LENGTH);
  }

  // Set the instrument name
  Variable *nameVar = instrument->FindVariable(FourCC::InstrumentName);
  if (nameVar) {
    nameVar->SetString(instrumentName.c_str());
  }

  // Mark the instrument as changed to trigger UI updates
  instrument->SetChanged();
  instrument->NotifyObservers();

  Trace::Log("PERSISTENCYSERVICE", "Successfully imported instrument settings");
  Trace::Log("PERSISTENCYSERVICE", "Set instrument name to: %s",
             instrumentName.c_str());
  return PERSIST_LOADED;
}
