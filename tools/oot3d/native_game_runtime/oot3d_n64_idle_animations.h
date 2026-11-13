#pragma once

// L3+R3 > Game > Gameplay > "Idle animations" (config/idle_animations: absent =
// N64's or OoT3D's at even odds, the default; "3ds" = OoT3D's; "n64" = N64's).
//
// Standing still, Link plays a fidget every other idle cycle. OoT3D picks it
// as N64 does (Player_ChooseNextIdleAnim, inlined in Player_Action_Idle at
// EUR 0x004BA538): the room's environment type (play +0x4C32) or, with a
// chance of 4 in 5 (Rand_ZeroOne 0x003759D0 times 5.0), one of the common
// fidgets (sword swing and shield only with the shield-hand model,
// rightHandType +0x1B5 == 10, the sword one only with a sword in hand), then
// the animation from the fidget table at 0x0053AB38 (15 pairs of u32
// animation ids, N64's 14 and OoT3D's ca_wait_typeA; the first of a pair with
// a one-handed sword in hand, modelAnimType +0x1B3 == 1, the second
// otherwise). The table still holds N64's animations, but right after the
// load OoT3D swaps two of them (block 0x004BA8CC..0x004BA8F8):
//   0x1A waitF_typeD_20f (stretch, unarmed; rooms of environment type 4-6,
//        e.g. Link's house, Kakariko, the Market) -> 0x1C waitF_itemA_20f
//        (tunic), and
//   0x15 wait_itemC_20f (shield fidget, both columns) -> 0x14
//        wait_itemD1_20f (sword swing).
// Both animations are still in zelda_link_boy_new.zar and
// zelda_link_child_new.zar (csab 0x1A and 0x15 of 582, the same files in both
// archives, 137 and 92 frames), and their sounds are still in the game's
// fidget sound tables (0x0053BFCC: stretch voice, shield sounds), which the
// same function looks up from the animation it finds in the fidget table.
//
// The hook at 0x004BA8CC (the block that loads the table entry, a block
// start, no generated code changes) leaves the graph at 0x004BA8FC, the
// instruction after the swap, with the registers the block would have left
// (r6 the table's animation; r0 and r1 as its own loads set them) when the
// entry is one of the two; every other entry runs the game's own block. What
// follows is the game's (the swim swap of the cold fidget, the play mode,
// LinkAnimation_Change). The armed stretch (wait_typeD_20f, 0x12) was never
// swapped. wait_itemD2_20f (0x13, two-handed sword swing) stays unused, as on
// N64: the selection reaches it only with a two-handed sword and a shield
// hand at once (zeldaret/oot z_player.c "@bug This code is unreachable"),
// and OoT3D's selection is the same. The rule is plain code, tested on the
// host (oot3d_n64_idle_animations_tests).
//
// Reference only: N64's table and selection, and the names in the comments,
// come from reading the zeldaret/oot decompilation (z_player.c,
// sFidgetAnimations, Player_ChooseNextIdleAnim). That repository carries no
// license, so no code is copied; the hook only skips OoT3D's own swap.

#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace Oot3dNativeGame {

// Player_Action_Idle: `ldr r0, =0x0053AB38` (the table entry's load).
inline constexpr std::uint32_t kN64IdleAnimationsHookPc = 0x004BA8CCU;
// Same function: `ldr r0, [r5, #0x710]`, after the swap.
inline constexpr std::uint32_t kN64IdleAnimationsResumePc = 0x004BA8FCU;

inline constexpr std::uint32_t kN64IdleFidgetTable = 0x0053AB38U;
inline constexpr std::uint32_t kN64IdleFidgetTypes = 15U;
// Player fields (EUR): r4 = player, r8 = player + 0x100 at the hook.
inline constexpr std::uint32_t kN64IdleModelAnimTypeOffset = 0x01B3U;  // modelAnimType (u8)
inline constexpr std::uint8_t kN64IdleModelAnimTypeSword = 1U;          // first column
inline constexpr std::uint32_t kN64IdleColumnByteOffset = 0x00ACU;     // [r8 + 0xAC], the block's other load

inline constexpr std::uint32_t kN64IdleStretchUnarmed = 0x1AU;  // waitF_typeD_20f
inline constexpr std::uint32_t kN64IdleShield = 0x15U;          // wait_itemC_20f

// The two entries OoT3D swaps (for 0x1C waitF_itemA_20f and 0x14
// wait_itemD1_20f).
constexpr bool IsSwappedN64IdleFidget(std::uint32_t animation) noexcept {
  return animation == kN64IdleStretchUnarmed || animation == kN64IdleShield;
}

// The fidget table entry the block loads.
constexpr std::uint32_t N64IdleFidgetEntry(std::uint32_t fidgetType, std::uint8_t modelAnimType) noexcept {
  return kN64IdleFidgetTable + fidgetType * 8U + (modelAnimType != kN64IdleModelAnimTypeSword ? 4U : 0U);
}

// At the hook, with the block's registers (r4 player, r6 fidget type, r8
// player + 0x100): true and the registers the block would leave without the
// swap if the entry is one it swaps; false (registers untouched, the game's
// block runs) otherwise or if a read fails.
template <class Bus>
bool RestoreN64IdleFidget(Bus &memory, std::array<std::uint32_t, 16> &r) {
  const std::uint32_t fidgetType = r[6];
  std::uint8_t modelAnimType = 0U;
  if (r[4] == 0U || fidgetType >= kN64IdleFidgetTypes ||
      !memory.Read8(r[4] + kN64IdleModelAnimTypeOffset, &modelAnimType))
    return false;
  const std::uint32_t entry = N64IdleFidgetEntry(fidgetType, modelAnimType);
  std::uint32_t animation = 0U;
  if (!memory.Read32(entry, &animation) || !IsSwappedN64IdleFidget(animation)) return false;
  std::uint32_t r0 = entry;
  if (modelAnimType != kN64IdleModelAnimTypeSword) {
    std::uint8_t column = 0U;
    if (!memory.Read8(r[8] + kN64IdleColumnByteOffset, &column)) return false;
    r0 = column;
  }
  r[0] = r0;
  r[1] = modelAnimType;
  r[6] = animation;
  return true;
}

// In the panel's order.
enum class IdleAnimations : std::uint8_t { Mixed = 0, Original3ds = 1, N64 = 2 };

// Read by the hook at every fidget choice, written by the panel.
inline std::atomic<IdleAnimations> &IdleAnimationsSetting() noexcept {
  static std::atomic<IdleAnimations> setting{IdleAnimations::Mixed};
  return setting;
}

// Whether this choice skips OoT3D's swap; `coin` is any random value (Mixed
// takes its lowest bit, so each swapped choice is N64's or OoT3D's at even odds).
constexpr bool UseN64IdleFidget(IdleAnimations setting, std::uint32_t coin) noexcept {
  return setting == IdleAnimations::N64 || (setting == IdleAnimations::Mixed && (coin & 1U) != 0U);
}

// A host random value for Mixed (xorshift32; the game's own random state is
// left alone, so the choice changes nothing else in the game).
inline std::uint32_t IdleAnimationsCoin() noexcept {
  static std::atomic<std::uint32_t> state{0x9E3779B9U};
  std::uint32_t x = state.load(std::memory_order_relaxed);
  x ^= x << 13U;
  x ^= x >> 17U;
  x ^= x << 5U;
  state.store(x, std::memory_order_relaxed);
  return x;
}

constexpr const char *IdleAnimationsName(IdleAnimations setting) noexcept {
  return setting == IdleAnimations::Mixed ? "mixed" : setting == IdleAnimations::N64 ? "n64" : "3ds";
}

inline constexpr const char *kIdleAnimationsConfigName = "idle_animations";

inline IdleAnimations ReadIdleAnimations(const std::filesystem::path &config) {
  std::ifstream file(config / kIdleAnimationsConfigName);
  std::string word;
  if (!(file >> word)) return IdleAnimations::Mixed;
  if (word == "3ds") return IdleAnimations::Original3ds;
  if (word == "n64") return IdleAnimations::N64;
  return IdleAnimations::Mixed;
}

// Removes the file (mixed) or writes the setting's name; false if that failed.
inline bool SaveIdleAnimations(const std::filesystem::path &config, IdleAnimations setting) {
  std::error_code error;
  if (setting == IdleAnimations::Mixed) {
    std::filesystem::remove(config / kIdleAnimationsConfigName, error);
    return !error;
  }
  std::filesystem::create_directories(config, error);
  std::ofstream file(config / kIdleAnimationsConfigName, std::ios::trunc);
  file << IdleAnimationsName(setting) << '\n';
  return static_cast<bool>(file);
}

} // namespace Oot3dNativeGame
