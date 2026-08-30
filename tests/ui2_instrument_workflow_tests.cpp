#include "doctest/doctest.h"

#include "Application/UI2/Workflows/Ui2InstrumentWorkflow.h"

#include <array>
#include <cstddef>
#include <cstring>

namespace {

struct FakeName {
  std::array<char, 12U> text{};

  [[nodiscard]] bool empty() const { return text[0] == '\0'; }
  [[nodiscard]] const char *c_str() const { return text.data(); }
};

struct FakeInstrument {
  InstrumentType type = IT_SAMPLE;
  int value = 0;
  FakeName name{};

  [[nodiscard]] InstrumentType GetType() const { return type; }
  [[nodiscard]] FakeName GetUserSetName() const { return name; }
};

class FakeBank {
public:
  class Replacement {
  public:
    ~Replacement() { Cancel(); }
    Replacement() = default;
    Replacement(const Replacement &) = delete;
    Replacement &operator=(const Replacement &) = delete;

    FakeInstrument *Candidate() { return candidate_; }
    bool Commit() {
      if (bank_ == nullptr || !bank_->commitAllowed_)
        return false;
      bank_->visible_ = candidate_;
      bank_->candidateInUse_ = false;
      bank_ = nullptr;
      candidate_ = nullptr;
      return true;
    }
    void Cancel() {
      if (bank_ != nullptr)
        bank_->candidateInUse_ = false;
      bank_ = nullptr;
      candidate_ = nullptr;
    }

  private:
    friend class FakeBank;
    FakeBank *bank_ = nullptr;
    FakeInstrument *candidate_ = nullptr;
  };

  FakeBank() { visible_ = &original_; }

  FakeInstrument *GetInstrument(unsigned short slot) {
    return slot == 0U ? visible_ : nullptr;
  }

  bool BeginReplacement(unsigned short slot, InstrumentType type,
                        Replacement &replacement) {
    ++replacementAttempts_;
    if (slot != 0U || allocationFailed_ || candidateInUse_)
      return false;
    candidate_ = {};
    candidate_.type = type;
    candidateInUse_ = true;
    replacement.bank_ = this;
    replacement.candidate_ = &candidate_;
    return true;
  }

  FakeInstrument original_{IT_SAMPLE, 73, {{{'O', 'L', 'D', '\0'}}}};
  FakeInstrument candidate_{};
  FakeInstrument *visible_ = nullptr;
  unsigned replacementAttempts_ = 0U;
  bool allocationFailed_ = false;
  bool commitAllowed_ = true;
  bool candidateInUse_ = false;
};

void SetName(FakeInstrument &instrument, const char *name) {
  instrument.name.text.fill('\0');
  if (name != nullptr)
    std::strncpy(instrument.name.text.data(), name,
                 instrument.name.text.size() - 1U);
}

} // namespace

TEST_CASE("UI2 Instrument workflow rejects unavailable and invalid imports") {
  using namespace ui2;
  FakeBank bank;
  unsigned detectorCalls = 0U;
  unsigned loaderCalls = 0U;
  const auto detector = [&](const char *) {
    ++detectorCalls;
    return IT_MIDI;
  };
  const auto loader = [&](FakeInstrument *) {
    ++loaderCalls;
    return true;
  };

  CHECK(Ui2InstrumentWorkflow::Import(&bank, 0U, "lead.pti", false,
                                      detector, loader) ==
        Ui2InstrumentImportOutcome::Unavailable);
  CHECK(detectorCalls == 0U);
  CHECK(loaderCalls == 0U);
  CHECK(bank.replacementAttempts_ == 0U);

  CHECK(Ui2InstrumentWorkflow::Import(
            &bank, 0U, "bad.pti", true,
            [&](const char *) {
              ++detectorCalls;
              return IT_NONE;
            },
            loader) == Ui2InstrumentImportOutcome::InvalidFile);
  CHECK(detectorCalls == 1U);
  CHECK(loaderCalls == 0U);
  CHECK(bank.visible_ == &bank.original_);
}

TEST_CASE("UI2 Instrument workflow maps every atomic import boundary") {
  using namespace ui2;
  FakeBank bank;
  const auto detector = [](const char *) { return IT_MIDI; };

  bank.allocationFailed_ = true;
  CHECK(Ui2InstrumentWorkflow::Import(
            &bank, 0U, "lead.pti", true, detector,
            [](FakeInstrument *) { return true; }) ==
        Ui2InstrumentImportOutcome::AllocationFailed);
  CHECK(bank.visible_ == &bank.original_);

  bank.allocationFailed_ = false;
  CHECK(Ui2InstrumentWorkflow::Import(
            &bank, 0U, "lead.pti", true, detector,
            [](FakeInstrument *candidate) {
              candidate->value = 11;
              return false;
            }) == Ui2InstrumentImportOutcome::RestoreFailed);
  CHECK(bank.visible_ == &bank.original_);
  CHECK_FALSE(bank.candidateInUse_);

  bank.commitAllowed_ = false;
  CHECK(Ui2InstrumentWorkflow::Import(
            &bank, 0U, "lead.pti", true, detector,
            [](FakeInstrument *candidate) {
              candidate->value = 11;
              return true;
            }) == Ui2InstrumentImportOutcome::CommitFailed);
  CHECK(bank.visible_ == &bank.original_);
  CHECK_FALSE(bank.candidateInUse_);

  bank.commitAllowed_ = true;
  CHECK(Ui2InstrumentWorkflow::Import(
            &bank, 0U, "lead.pti", true, detector,
            [](FakeInstrument *candidate) {
              candidate->value = 11;
              return true;
            }) == Ui2InstrumentImportOutcome::Imported);
  CHECK(bank.visible_ == &bank.candidate_);
  CHECK(bank.visible_->type == IT_MIDI);
  CHECK(bank.visible_->value == 11);
}

TEST_CASE("UI2 Instrument workflow rechecks playback at type commit") {
  using namespace ui2;
  FakeBank bank;

  CHECK(Ui2InstrumentWorkflow::ChangeType(&bank, 0U, IT_MIDI, true) ==
        Ui2InstrumentTypeOutcome::PlayingBlocked);
  CHECK(bank.replacementAttempts_ == 0U);
  CHECK(bank.visible_ == &bank.original_);

  CHECK(Ui2InstrumentWorkflow::ChangeType(&bank, 0U, IT_SAMPLE, true) ==
        Ui2InstrumentTypeOutcome::NoChange);
  CHECK(bank.replacementAttempts_ == 0U);

  bank.allocationFailed_ = true;
  CHECK(Ui2InstrumentWorkflow::ChangeType(&bank, 0U, IT_MIDI, false) ==
        Ui2InstrumentTypeOutcome::AllocationFailed);
  CHECK(bank.visible_ == &bank.original_);

  bank.allocationFailed_ = false;
  CHECK(Ui2InstrumentWorkflow::ChangeType(&bank, 0U, IT_MIDI, false) ==
        Ui2InstrumentTypeOutcome::Changed);
  CHECK(bank.visible_ == &bank.candidate_);
  CHECK(bank.visible_->type == IT_MIDI);
}

TEST_CASE("UI2 Instrument type failures define visible feedback") {
  using namespace ui2;

  CHECK(std::strcmp(Ui2InstrumentTypeFailureText(
                        Ui2InstrumentTypeOutcome::AllocationFailed),
                    "NO FREE INSTRUMENT SLOT") == 0);
  CHECK(std::strcmp(Ui2InstrumentTypeFailureText(
                        Ui2InstrumentTypeOutcome::Unavailable),
                    "INSTRUMENT TYPE UNAVAILABLE") == 0);
  CHECK(std::strcmp(Ui2InstrumentTypeFailureText(
                        Ui2InstrumentTypeOutcome::CommitFailed),
                    "INSTRUMENT TYPE FAILED") == 0);
  CHECK(std::strcmp(
            Ui2InstrumentTypeFailureText(Ui2InstrumentTypeOutcome::Changed),
            "") == 0);
  CHECK(std::strcmp(
            Ui2InstrumentTypeFailureText(Ui2InstrumentTypeOutcome::NoChange),
            "") == 0);
  CHECK(std::strcmp(Ui2InstrumentTypeFailureText(
                        Ui2InstrumentTypeOutcome::PlayingBlocked),
                    "") == 0);
}

TEST_CASE("UI2 Instrument workflow validates and maps exports") {
  using namespace ui2;
  FakeInstrument instrument;
  unsigned exportCalls = 0U;
  const auto save = [&](FakeInstrument *, const char *, bool) {
    ++exportCalls;
    return Ui2InstrumentStorageResult::Saved;
  };

  CHECK(Ui2InstrumentWorkflow::Export<FakeInstrument>(nullptr, false, save) ==
        Ui2InstrumentExportOutcome::NoInstrument);
  instrument.type = IT_NONE;
  CHECK(Ui2InstrumentWorkflow::Export(&instrument, false, save) ==
        Ui2InstrumentExportOutcome::NoInstrument);
  instrument.type = IT_SAMPLE;
  CHECK(Ui2InstrumentWorkflow::Export(&instrument, false, save) ==
        Ui2InstrumentExportOutcome::MissingName);
  CHECK(exportCalls == 0U);

  SetName(instrument, "LEAD");
  CHECK(Ui2InstrumentWorkflow::Export(&instrument, false, save) ==
        Ui2InstrumentExportOutcome::Saved);
  CHECK(exportCalls == 1U);
  CHECK(Ui2InstrumentWorkflow::Export(
            &instrument, false,
            [](FakeInstrument *, const char *, bool) {
              return Ui2InstrumentStorageResult::Exists;
            }) == Ui2InstrumentExportOutcome::Exists);
  CHECK(Ui2InstrumentWorkflow::Export(
            &instrument, true,
            [](FakeInstrument *, const char *, bool) {
              return Ui2InstrumentStorageResult::Exists;
            }) == Ui2InstrumentExportOutcome::Failed);
}

TEST_CASE("UI2 Instrument workflow rejects rename on the shared empty slot") {
  using namespace ui2;
  FakeInstrument instrument;

  CHECK_FALSE(Ui2InstrumentWorkflow::CanRename<FakeInstrument>(nullptr));
  instrument.type = IT_NONE;
  CHECK_FALSE(Ui2InstrumentWorkflow::CanRename(&instrument));
  instrument.type = IT_SAMPLE;
  CHECK(Ui2InstrumentWorkflow::CanRename(&instrument));
}

TEST_CASE("UI2 Instrument export outcomes define visible feedback") {
  using namespace ui2;

  const Ui2InstrumentExportFeedback saved =
      Ui2InstrumentExportFeedbackFor(Ui2InstrumentExportOutcome::Saved);
  CHECK(std::strcmp(saved.text, "INSTRUMENT SAVED") == 0);
  CHECK_FALSE(saved.error);

  const Ui2InstrumentExportFeedback missing =
      Ui2InstrumentExportFeedbackFor(Ui2InstrumentExportOutcome::MissingName);
  CHECK(std::strcmp(missing.text, "NAME INSTRUMENT FIRST") == 0);
  CHECK(missing.error);

  const Ui2InstrumentExportFeedback exists =
      Ui2InstrumentExportFeedbackFor(Ui2InstrumentExportOutcome::Exists);
  CHECK(std::strcmp(exists.text, "") == 0);
  CHECK_FALSE(exists.error);

  const Ui2InstrumentExportFeedback failed =
      Ui2InstrumentExportFeedbackFor(Ui2InstrumentExportOutcome::Failed);
  CHECK(std::strcmp(failed.text, "INSTRUMENT SAVE FAILED") == 0);
  CHECK(failed.error);
}

TEST_CASE("UI2 Instrument workflow keeps browser failure copy stable") {
  CHECK(std::strcmp(ui2::Ui2InstrumentImportFailureText(
                        ui2::Ui2InstrumentImportOutcome::InvalidFile),
                    "INVALID INSTRUMENT FILE") == 0);
  CHECK(std::strcmp(ui2::Ui2InstrumentImportFailureText(
                        ui2::Ui2InstrumentImportOutcome::CommitFailed),
                    "INSTRUMENT LOAD FAILED") == 0);
}
