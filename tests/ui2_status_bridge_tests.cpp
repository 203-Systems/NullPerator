#include "doctest/doctest.h"

#include "Application/UI2/Ui2StatusBridge.h"

#include <cstring>

namespace {

class MutatingLegacyStatus final : public Status {
public:
  void Print(char *text) override {
    ++singleCalls;
    Mutate(text);
  }

  void PrintMultiLine(char *text) override {
    ++multiCalls;
    Mutate(text);
  }

  int singleCalls = 0;
  int multiCalls = 0;

private:
  static void Mutate(char *text) {
    if (text != nullptr && text[0] != '\0')
      text[0] = '!';
  }
};

} // namespace

TEST_CASE("UI2 status bridge captures formatted status without heap state") {
  Status::Install(nullptr);
  ui2::Ui2StatusBridge bridge;
  bridge.Attach();

  Status::Set("Loading %u%%", 42U);
  const ui2::Ui2StatusSnapshot single = bridge.Read();
  CHECK(single.hasValue);
  CHECK(single.layout == ui2::Ui2StatusLayout::SingleLine);
  CHECK(std::strcmp(single.text.data(), "Loading 42%") == 0);

  Status::SetMultiLine("Invalid Project:\n%s", "demo");
  const ui2::Ui2StatusSnapshot multi = bridge.Read();
  CHECK(multi.hasValue);
  CHECK(multi.revision == single.revision + 1U);
  CHECK(multi.layout == ui2::Ui2StatusLayout::MultiLine);
  CHECK(std::strcmp(multi.text.data(), "Invalid Project:\ndemo") == 0);

  bridge.Clear();
  const ui2::Ui2StatusSnapshot cleared = bridge.Read();
  CHECK_FALSE(cleared.hasValue);
  CHECK(cleared.revision == multi.revision + 1U);
  CHECK(cleared.text[0] == '\0');
}

TEST_CASE("UI2 status bridge forwards and restores a legacy sink") {
  MutatingLegacyStatus legacy;
  Status::Install(&legacy);

  {
    ui2::Ui2StatusBridge bridge;
    bridge.Attach();
    CHECK(Status::GetInstance() == &bridge);

    Status::Set("ready");
    const ui2::Ui2StatusSnapshot snapshot = bridge.Read();
    CHECK(legacy.singleCalls == 1);
    CHECK(std::strcmp(snapshot.text.data(), "ready") == 0);

    Status::SetMultiLine("one\ntwo");
    CHECK(legacy.multiCalls == 1);
  }

  CHECK(Status::GetInstance() == &legacy);
  Status::Install(nullptr);
}

TEST_CASE("UI2 status bridge detach does not replace a newer owner") {
  MutatingLegacyStatus prior;
  MutatingLegacyStatus replacement;
  Status::Install(&prior);

  ui2::Ui2StatusBridge bridge;
  bridge.Attach();
  Status::Install(&replacement);
  bridge.Detach();

  CHECK(Status::GetInstance() == &replacement);
  Status::Install(nullptr);
}
