#pragma once

// Which of the game's message L/R button icons read ZL/ZR
// (fast/oot3d/message_button_glyphs.h), from the controller bindings.

#include "fast/oot3d/message_button_glyphs.h"
#include "oot3d_native_control_config.h"

#include <cstdint>

namespace Oot3dNativeGame {

// True on the Switch: its SDL mapping crosses the shoulders on purpose
// (ConnectedPhysicalDeviceManager: leftshoulder is ZL, lefttrigger is L), so
// a LeftShoulder binding is the button labelled ZL. Elsewhere a shoulder is
// the controller's L/R and a trigger its ZL/ZR (LT/RT).
#if defined(__SWITCH__)
inline constexpr bool kGamepadShouldersAreZlZr = true;
#else
inline constexpr bool kGamepadShouldersAreZlZr = false;
#endif

// The 3DS L icon reads ZL when the game's L is bound to the button labelled
// ZL, likewise R/ZR; any other binding keeps the game's icon. The Switch
// defaults put L/R on ZL/ZR; Controls > "Swap shoulders / triggers" moves them
// to L/R, where the game's icons are right again.
inline std::uint8_t MessageShoulderGlyphsForBindings(const NativeControlConfig& config,
                                                     bool shouldersAreZlZr = kGamepadShouldersAreZlZr) noexcept {
  if (!config.ControllerEnabled) return 0U;
  const auto gamepad = [&](NativeControlAction action) {
    return config.Bindings[static_cast<std::size_t>(action)].Gamepad;
  };
  const NativeGamepadButton zl =
      shouldersAreZlZr ? NativeGamepadButton::LeftShoulder : NativeGamepadButton::LeftTrigger;
  const NativeGamepadButton zr =
      shouldersAreZlZr ? NativeGamepadButton::RightShoulder : NativeGamepadButton::RightTrigger;
  std::uint8_t mask = 0U;
  if (gamepad(NativeControlAction::L) == zl) mask |= Fast::Oot3d::kMessageGlyphZl;
  if (gamepad(NativeControlAction::R) == zr) mask |= Fast::Oot3d::kMessageGlyphZr;
  return mask;
}

} // namespace Oot3dNativeGame
