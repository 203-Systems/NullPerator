#include "Adapters/node/hal/nullperator/power/power.h"
#include "Adapters/node/hal/nullperator/system/io_expander.h"
#include "Adapters/node/hal/nullperator/system/system.h"
#include "board/pins.h"
#include "doctest/doctest.h"

namespace {
using NullperatorHAL::System::IOExpander;
constexpr uint16_t kChargingBit = 1U << PCA_BTN_CHRG;
constexpr uint16_t kFullBit = 1U << PCA_BTN_FULL;
struct Fixture {
  IOExpander expander;
  uint16_t physical = 0xffff;
  esp_err_t result = ESP_OK;
  unsigned reads = 0;
  bool ready = true;
  Fixture();
};
Fixture *active = nullptr;

Fixture::Fixture() {
  active = this;
  expander.Attach(this);
}
} // namespace

esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t handle,
                                    const uint8_t *reg, std::size_t regSize,
                                    uint8_t *data, std::size_t size, int) {
  REQUIRE(active != nullptr);
  CHECK(handle == active);
  REQUIRE(regSize == 1);
  CHECK(*reg == 0);
  REQUIRE(size == 2);
  ++active->reads;
  // Write even on errors to model a failed/partial transfer. Callers must not
  // treat these bytes (or a prior cached sample) as valid charge status.
  data[0] = static_cast<uint8_t>(active->physical);
  data[1] = static_cast<uint8_t>(active->physical >> 8U);
  return active->result;
}

esp_err_t i2c_master_transmit(i2c_master_dev_handle_t, const uint8_t *,
                            std::size_t, int) {
  return ESP_OK;
}

namespace NullperatorHAL::System {
bool ReadIOExpanderChecked(uint16_t &value) {
  return active->ready && active->expander.ReadChecked(value);
}
} // namespace NullperatorHAL::System

TEST_CASE("Node charge status uses active-low CHRG, not FULL or button bits") {
  Fixture fixture;
  for (uint32_t bits = 0; bits <= 0xffffU; ++bits) {
    fixture.physical = static_cast<uint16_t>(bits);
    bool charging = false;
    REQUIRE(NullperatorHAL::Power::ReadChargingState(charging));
    CHECK(charging == ((bits & kChargingBit) == 0));
  }
  // A FULL indication is not an ongoing charge indication.
  fixture.physical = static_cast<uint16_t>(0xffffU & ~kFullBit);
  CHECK_FALSE(NullperatorHAL::Power::IsCharging());
}

TEST_CASE("Node charge status reads transitions without stale button cache") {
  Fixture fixture;
  fixture.physical = static_cast<uint16_t>(0xffffU & ~kChargingBit);
  CHECK(NullperatorHAL::Power::IsCharging());
  CHECK(fixture.expander.Read() == fixture.physical);

  fixture.physical = 0xffff;
  CHECK_FALSE(NullperatorHAL::Power::IsCharging());
  fixture.physical &= ~kChargingBit;
  CHECK(NullperatorHAL::Power::IsCharging());
}

TEST_CASE("Node unavailable expander never claims charging") {
  Fixture fixture;
  fixture.ready = false;
  bool charging = true;
  CHECK_FALSE(NullperatorHAL::Power::ReadChargingState(charging));
  CHECK(charging); // Invalid samples must not overwrite the caller's output.
  CHECK_FALSE(NullperatorHAL::Power::IsCharging());
  CHECK(fixture.reads == 0);

  IOExpander unattached;
  uint16_t value = 0x1234;
  CHECK_FALSE(unattached.ReadChecked(value));
  CHECK(value == 0x1234);
  CHECK(fixture.reads == 0);
}

TEST_CASE("Node failed charge read cannot reuse active-low zero or cached data") {
  Fixture fixture;
  fixture.physical = 0;
  CHECK(fixture.expander.Read() == 0);
  fixture.result = ESP_ERR_TIMEOUT;

  uint16_t value = 0x1234;
  CHECK_FALSE(fixture.expander.ReadChecked(value));
  CHECK(value == 0x1234);
  bool charging = false;
  CHECK_FALSE(NullperatorHAL::Power::ReadChargingState(charging));
  CHECK_FALSE(charging);
  CHECK_FALSE(NullperatorHAL::Power::IsCharging());

  fixture.result = ESP_OK;
  CHECK(NullperatorHAL::Power::ReadChargingState(charging));
  CHECK(charging);
  fixture.result = ESP_FAIL;
  CHECK_FALSE(NullperatorHAL::Power::IsCharging());
}

TEST_CASE("Node checked input read decodes both ports in little-endian order") {
  Fixture fixture;
  fixture.physical = 0xa5c3;
  uint16_t value = 0;
  REQUIRE(fixture.expander.ReadChecked(value));
  CHECK(value == 0xa5c3);
  CHECK(fixture.reads == 1);
}
