// N64 idle animations (oot3d_n64_idle_animations.h): the hook at the fidget
// table load of Player_Action_Idle (0x004BA8CC), against a model of the
// game's block with and without OoT3D's swap, and the config file.
#include "oot3d_n64_idle_animations.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>

namespace {

using namespace Oot3dNativeGame;

void Require(bool condition, const char *message) {
  if (!condition) {
    std::fprintf(stderr, "FAIL: %s\n", message);
    std::exit(1);
  }
}

// The EUR fidget table at 0x0053AB38 (code.bin), [type][column].
constexpr std::uint32_t kTable[kN64IdleFidgetTypes][2] = {
    {0x4F, 0x56},   // look around: nml_wait_typeA_20f, nml_waitF_typeA_20f
    {0x4D, 0x54},   // cold: nml_wait(F)_typeC_20f
    {0x4E, 0x55},   // warm: nml_wait(F)_typeB_20f
    {0x4E, 0x55},   // hot
    {0x12, 0x1A},   // stretch 1: wait_typeD_20f, waitF_typeD_20f
    {0x12, 0x1A},   // stretch 2
    {0x12, 0x1A},   // stretch 3
    {0x19, 0x1E},   // critical health start: wait(F)_heat1_20f
    {0x18, 0x1D},   // critical health loop: wait(F)_heat2_20f
    {0x14, 0x14},   // sword swing: wait_itemD1_20f
    {0x17, 0x1C},   // tunic: wait(F)_itemA_20f
    {0x16, 0x1B},   // tap feet: wait(F)_itemB_20f
    {0x15, 0x15},   // shield: wait_itemC_20f
    {0x13, 0x13},   // two-handed sword swing: wait_itemD2_20f (unreachable, as on N64)
    {0x1FB, 0x1FB}, // OoT3D: ca_wait_typeA
};

constexpr std::uint32_t kPlayer = 0x08100000U;

// Sparse little-endian guest memory; a read of an unwritten byte fails.
class FakeBus {
public:
  FakeBus() {
    for (std::uint32_t type = 0U; type < kN64IdleFidgetTypes; ++type)
      for (std::uint32_t column = 0U; column < 2U; ++column)
        Put32(kN64IdleFidgetTable + type * 8U + column * 4U, kTable[type][column]);
  }
  void Put8(std::uint32_t address, std::uint8_t value) { mBytes[address] = value; }
  void Put32(std::uint32_t address, std::uint32_t value) {
    for (std::uint32_t i = 0U; i < 4U; ++i) Put8(address + i, static_cast<std::uint8_t>(value >> (8U * i)));
  }
  void Forget(std::uint32_t address) { mBytes.erase(address); }
  bool Read8(std::uint32_t address, std::uint8_t *value) const {
    const auto found = mBytes.find(address);
    if (found == mBytes.end()) return false;
    *value = found->second;
    return true;
  }
  bool Read32(std::uint32_t address, std::uint32_t *value) const {
    std::uint32_t result = 0U;
    for (std::uint32_t i = 0U; i < 4U; ++i) {
      std::uint8_t byte = 0U;
      if (!Read8(address + i, &byte)) return false;
      result |= static_cast<std::uint32_t>(byte) << (8U * i);
    }
    *value = result;
    return true;
  }

private:
  std::map<std::uint32_t, std::uint8_t> mBytes;
};

constexpr std::uint8_t kColumnByte = 0x5AU;  // [player + 0x1AC]

FakeBus Player(std::uint8_t modelAnimType) {
  FakeBus bus;
  bus.Put8(kPlayer + kN64IdleModelAnimTypeOffset, modelAnimType);
  bus.Put8(kPlayer + 0x100U + kN64IdleColumnByteOffset, kColumnByte);
  return bus;
}

std::array<std::uint32_t, 16> Registers(std::uint32_t fidgetType) {
  std::array<std::uint32_t, 16> r{};
  for (std::uint32_t i = 0U; i < r.size(); ++i) r[i] = 0xC0DE0000U + i;
  r[4] = kPlayer;
  r[6] = fidgetType;
  r[8] = kPlayer + 0x100U;
  return r;
}

// The game's block 0x004BA8CC..0x004BA8F8 (r0, r1, r6 at 0x004BA8FC), with
// OoT3D's swap or without it (N64's table lookup).
std::array<std::uint32_t, 16> GameBlock(const FakeBus &bus, std::array<std::uint32_t, 16> r, bool swap) {
  std::uint8_t modelAnimType = 0U;
  Require(bus.Read8(r[4] + 0x1B3U, &modelAnimType), "model: modelAnimType");
  std::uint32_t entry = 0x0053AB38U + r[6] * 8U;          // ldr r0, =table; add r0, r0, r6, lsl #3
  r[1] = modelAnimType;                                    // ldrb r1, [r4, #0x1B3]
  if (modelAnimType != 1U) entry += 4U;                    // cmp r1, #1; addne r0, r0, #4
  r[0] = entry;
  Require(bus.Read32(entry, &r[6]), "model: the entry");   // ldr r6, [r0]
  if (modelAnimType != 1U) {                               // ldrbne r0, [r8, #0xAC]
    std::uint8_t byte = 0U;
    Require(bus.Read8(r[8] + 0xACU, &byte), "model: the column byte");
    r[0] = byte;
  }
  if (swap) {
    if (r[6] == 0x1AU) r[6] = 0x1CU;                       // cmp r6, #0x1A; moveq r6, #0x1C
    else if (r[6] == 0x15U) r[6] = 0x14U;                  // cmp r6, #0x15; moveq r6, #0x14
  }
  return r;
}

void TestEveryFidget() {
  int restored = 0;
  for (const std::uint8_t modelAnimType : std::array<std::uint8_t, 4>{0U, 1U, 2U, 3U}) {
    const FakeBus bus = Player(modelAnimType);
    for (std::uint32_t type = 0U; type < kN64IdleFidgetTypes; ++type) {
      auto r = Registers(type);
      const auto original = r;
      const auto n64 = GameBlock(bus, original, false);
      const auto oot3d = GameBlock(bus, original, true);
      if (RestoreN64IdleFidget(bus, r)) {
        ++restored;
        Require(r == n64, "a restored fidget leaves the block's registers without the swap");
        Require(oot3d[6] != n64[6], "only an entry OoT3D swaps is restored");
      } else {
        Require(r == original, "the game's block runs: registers untouched");
        Require(oot3d == n64, "every entry OoT3D keeps runs the game's block");
      }
    }
  }
  // Stretch 1-3 with anything but a one-handed sword in hand (3 types x 3
  // model types), and the shield fidget in both columns (4 model types).
  Require(restored == 3 * 3 + 4, "the unarmed stretch and the shield fidget, nothing else");
}

void TestTheTwoAnimations() {
  {
    const FakeBus bus = Player(0U);
    auto r = Registers(5U);
    Require(RestoreN64IdleFidget(bus, r) && r[6] == kN64IdleStretchUnarmed && r[0] == kColumnByte && r[1] == 0U,
            "unarmed in Link's house (environment 5): waitF_typeD_20f, not the tunic");
  }
  {
    const FakeBus bus = Player(1U);
    auto r = Registers(5U);
    Require(!RestoreN64IdleFidget(bus, r), "sword in hand: wait_typeD_20f, which OoT3D never swapped");
  }
  {
    const FakeBus bus = Player(1U);
    auto r = Registers(12U);
    Require(RestoreN64IdleFidget(bus, r) && r[6] == kN64IdleShield && r[0] == kN64IdleFidgetTable + 12U * 8U &&
                r[1] == 1U,
            "sword and shield in hand: wait_itemC_20f, not the sword swing");
  }
  {
    const FakeBus bus = Player(0U);
    auto r = Registers(9U);
    Require(!RestoreN64IdleFidget(bus, r), "the sword swing itself stays the game's");
  }
  {
    const FakeBus bus = Player(0U);
    auto r = Registers(13U);
    Require(!RestoreN64IdleFidget(bus, r), "wait_itemD2_20f is the game's entry (and unreachable, as on N64)");
  }
}

void TestRefusals() {
  const auto refused = [](FakeBus bus, std::array<std::uint32_t, 16> r, const char *message) {
    const auto original = r;
    Require(!RestoreN64IdleFidget(bus, r) && r == original, message);
  };
  refused(Player(0U), Registers(kN64IdleFidgetTypes), "a fidget type past the table");
  auto noPlayer = Registers(5U);
  noPlayer[4] = 0U;
  refused(Player(0U), noPlayer, "no player");
  FakeBus noModel = Player(0U);
  noModel.Forget(kPlayer + kN64IdleModelAnimTypeOffset);
  refused(noModel, Registers(5U), "modelAnimType unreadable");
  FakeBus noTable = Player(0U);
  noTable.Forget(N64IdleFidgetEntry(5U, 0U));
  refused(noTable, Registers(5U), "the table unreadable");
  FakeBus noColumn = Player(0U);
  noColumn.Forget(kPlayer + 0x100U + kN64IdleColumnByteOffset);
  refused(noColumn, Registers(5U), "the block's other load unreadable");
}

void TestConfig() {
  Require(N64IdleAnimationsEnabled().load(), "on by default");
  Require(kN64IdleAnimationsHookPc < kN64IdleAnimationsResumePc, "the hook, then past the swap");
  const auto config = std::filesystem::temp_directory_path() / "oot3d_n64_idle_animations_tests";
  std::error_code ignored;
  std::filesystem::remove_all(config, ignored);
  Require(ReadN64IdleAnimationsEnabled(config), "no file: on");
  Require(SaveN64IdleAnimationsEnabled(config, false) && !ReadN64IdleAnimationsEnabled(config),
          "off writes the file");
  Require(std::filesystem::exists(config / "no_n64_idle_animations"), "file name");
  Require(SaveN64IdleAnimationsEnabled(config, true) && ReadN64IdleAnimationsEnabled(config), "on removes it");
  Require(SaveN64IdleAnimationsEnabled(config, true), "on twice");
  std::filesystem::remove_all(config, ignored);
}

} // namespace

int main() {
  TestEveryFidget();
  TestTheTwoAnimations();
  TestRefusals();
  TestConfig();
  std::puts("oot3d_n64_idle_animations_tests: ok");
  return 0;
}
