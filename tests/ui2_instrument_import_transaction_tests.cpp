#include "doctest/doctest.h"

#include "Application/UI2/Ui2InstrumentImportTransaction.h"

#include <array>
#include <cstdint>

namespace {

enum class FakeType : std::uint8_t { Sample, Midi };

struct FakeInstrument {
  FakeType type = FakeType::Sample;
  int field = 0;
  std::array<char, 8> name{};
};

class FakeBank {
public:
  class Replacement {
  public:
    ~Replacement() { Cancel(); }
    Replacement(const Replacement &) = delete;
    Replacement &operator=(const Replacement &) = delete;
    Replacement() = default;

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

  bool BeginReplacement(unsigned short slot, FakeType type,
                        Replacement &replacement) {
    if (slot != 0U || allocationExhausted_ || candidateInUse_)
      return false;
    candidate_ = {};
    candidate_.type = type;
    candidateInUse_ = true;
    replacement.bank_ = this;
    replacement.candidate_ = &candidate_;
    return true;
  }

  FakeInstrument original_{FakeType::Sample, 73, {'O', 'L', 'D'}};
  FakeInstrument candidate_{};
  FakeInstrument *visible_ = nullptr;
  bool allocationExhausted_ = false;
  bool commitAllowed_ = true;
  bool candidateInUse_ = false;
};

ui2::Ui2InstrumentImportResult Import(FakeBank &bank, bool restoreSucceeds,
                                      unsigned &dirtyGeneration) {
  const auto result = ui2::Ui2ImportInstrumentAtomically(
      bank, 0U, FakeType::Midi, [&](FakeInstrument *candidate) {
        candidate->field = 11;
        candidate->name = {'N', 'E', 'W'};
        return restoreSucceeds;
      });
  if (result == ui2::Ui2InstrumentImportResult::Imported)
    ++dirtyGeneration;
  return result;
}

void CheckOriginalUntouched(const FakeBank &bank) {
  CHECK(bank.visible_ == &bank.original_);
  CHECK(bank.visible_->type == FakeType::Sample);
  CHECK(bank.visible_->field == 73);
  CHECK(bank.visible_->name[0] == 'O');
  CHECK(bank.visible_->name[1] == 'L');
  CHECK(bank.visible_->name[2] == 'D');
}

} // namespace

TEST_CASE("UI2 instrument import atomically replaces a cross-type slot") {
  FakeBank bank;
  unsigned dirtyGeneration = 0;
  CHECK(Import(bank, true, dirtyGeneration) ==
        ui2::Ui2InstrumentImportResult::Imported);
  CHECK(bank.visible_ == &bank.candidate_);
  CHECK(bank.visible_->type == FakeType::Midi);
  CHECK(bank.visible_->field == 11);
  CHECK(bank.visible_->name[0] == 'N');
  CHECK(dirtyGeneration == 1U);
  CHECK_FALSE(bank.candidateInUse_);
}

TEST_CASE("UI2 instrument import rolls back a failed Restore exactly") {
  FakeBank bank;
  unsigned dirtyGeneration = 0;
  CHECK(Import(bank, false, dirtyGeneration) ==
        ui2::Ui2InstrumentImportResult::RestoreFailed);
  CheckOriginalUntouched(bank);
  CHECK(dirtyGeneration == 0U);
  CHECK_FALSE(bank.candidateInUse_);
}

TEST_CASE("UI2 instrument import preserves the slot on pool exhaustion") {
  FakeBank bank;
  bank.allocationExhausted_ = true;
  unsigned dirtyGeneration = 0;
  CHECK(Import(bank, true, dirtyGeneration) ==
        ui2::Ui2InstrumentImportResult::AllocationFailed);
  CheckOriginalUntouched(bank);
  CHECK(dirtyGeneration == 0U);
  CHECK_FALSE(bank.candidateInUse_);
}

TEST_CASE("UI2 instrument import discards a restored candidate if commit fails") {
  FakeBank bank;
  bank.commitAllowed_ = false;
  unsigned dirtyGeneration = 0;
  CHECK(Import(bank, true, dirtyGeneration) ==
        ui2::Ui2InstrumentImportResult::CommitFailed);
  CheckOriginalUntouched(bank);
  CHECK(dirtyGeneration == 0U);
  CHECK_FALSE(bank.candidateInUse_);
}
