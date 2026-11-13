#include "oot3d_top_screen_ocarina.h"
#include "oot3d_native_a32_memory.h"
#include "oot3d_ui/ui_contract_types.h"
#include <bit>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <utility>

namespace {
void Require(bool condition, const char *message) {
  if (!condition) { std::cerr << "TopScreen ocarina: " << message << '\n'; std::exit(1); }
}
constexpr std::uint32_t kMessageContext = 0x005C1878U;
constexpr std::uint32_t kStaffNoteSlots = kMessageContext + 0x44U + 0x954U;
Oot3dNativeGame::NativeA32Memory BuildOcarinaFixture() {
  using namespace Oot3dNativeGame;
  NativeA32Memory memory;
  std::string error;
  Require(memory.MapRegion({"topscreen-ocarina-fixture",
                            0x004D0000U,
                            0x000C0000U,
                            true,
                            false,
                            {}},
                           &error),
          "could not map ocarina fixture");
  // The message context (play + 0x32C0) as free play leaves it: open, in
  // box state 3, but +0xD cleared, so 0x0042CBCC draws no box. The game
  // empties the eight staff note slots (+0x44 + 0x954, stride 8) when the
  // ocarina starts.
  Require(memory.MapRegion({"topscreen-ocarina-message", 0x005C1000U, 0x2000U,
                            true, false, {}},
                           &error),
          "could not map ocarina message fixture");
  Require(memory.Write8(kMessageContext + 0xCU, 1U) &&
              memory.Write8(kMessageContext + 0xDU, 0U) &&
              memory.Write8(kMessageContext + 0xEU, 1U) &&
              memory.Write8(kMessageContext + 0xF38U, 3U),
          "could not seed the free-play message context");
  for (std::uint32_t slot = 0U; slot < 8U; ++slot) {
    Require(memory.Write32(kStaffNoteSlots + slot * 8U, 0xFFU),
            "could not seed an empty ocarina staff");
  }
  const auto writeFloat = [&memory](std::uint32_t address, float value) {
    return memory.Write32(address, std::bit_cast<std::uint32_t>(value));
  };
  constexpr std::uint32_t kOcarina = 0x005093E4U;
  constexpr std::uint32_t kScene = 0x00520000U;
  constexpr std::uint32_t kNoteTypes = 0x00521000U;
  Require(memory.Write32(0x005043D4U + 0x0CU, kScene) &&
              memory.Write32(0x0050AF68U, 2U) &&
              memory.Write8(kScene + 0x100U, 3U) &&
              memory.Write8(kScene + 0x101U, 2U) &&
              memory.Write16(kScene + 0x2B82U, 0xFFU) &&
              memory.Write16(kScene + 0x2B80U, 1U) &&
              memory.Write32(kOcarina + 0x14U, 4U) &&
              memory.Write32(kOcarina + 0x18U, 1U) &&
              memory.Write32(kOcarina + 0x1CU, 1U) &&
              memory.Write32(kOcarina + 0x38U, 0U) &&
              memory.Write32(kOcarina + 0x44U, 1U),
          "could not seed ocarina controller state");
  for (const std::uint32_t index : {0U, 1U, 2U, 35U, 36U, 37U}) {
    Require(writeFloat(kOcarina + 0x68U + index * 8U,
                       10.0F + static_cast<float>(index)) &&
                writeFloat(kOcarina + 0x6CU + index * 8U,
                           20.0F + static_cast<float>(index)) &&
                writeFloat(kOcarina + 0x3C8U + index * 8U, 30.0F) &&
                writeFloat(kOcarina + 0x3CCU + index * 8U, 40.0F) &&
                writeFloat(kOcarina + 0x728U + index * 8U, 50.0F) &&
                writeFloat(kOcarina + 0x72CU + index * 8U, 60.0F) &&
                writeFloat(kOcarina + 0xA88U + index * 8U, 70.0F) &&
                writeFloat(kOcarina + 0xA8CU + index * 8U, 80.0F),
            "could not seed ocarina native geometry arrays");
  }
  constexpr std::uint32_t kDestination = 5U;
  Require(memory.Write32(0x00587A14U, 4U) &&
              memory.Write32(0x0050A3B0U + kDestination * 4U, 100U) &&
              memory.Write32(0x0053C9D4U + 100U * 4U, 4U) &&
              memory.Write32(0x004D53C8U + kDestination * 4U, 2U) &&
              memory.Write32(0x004D541CU + kDestination * 4U, kNoteTypes) &&
              memory.Write32(kNoteTypes, 1U) &&
              memory.Write32(kNoteTypes + 4U, 4U),
          "could not seed ocarina destination tables");
  for (std::uint32_t type = 0U; type < 5U; ++type) {
    Require(
        writeFloat(0x0050A1CCU + type * 4U, 5.0F + static_cast<float>(type)) &&
            writeFloat(0x0050A1E0U + type * 4U,
                       90.0F + static_cast<float>(type)),
        "could not seed ocarina marker tables");
  }
  return memory;
}

} // namespace

void RunTopScreenOcarinaTests() {
  using namespace Oot3dNativeGame;
  auto ocarinaMemory = BuildOcarinaFixture();
  TopScreenOcarinaBrowser ocarinaBrowser;
  TopScreenOcarinaGeometry ocarinaGeometry;
  std::string ocarinaError;
  Require(ReadTopScreenOcarinaGeometry(ocarinaMemory, ocarinaBrowser, &ocarinaGeometry,
                                        &ocarinaError) &&
              ocarinaGeometry.Active && ocarinaGeometry.Song == 5U,
          "ocarina producer did not activate the native destination");
  std::uint32_t message = 0;
  Require(ocarinaMemory.Write32(0x004D5480U + 5U * 4U, 8U) &&
              ReadTopScreenOcarinaSongMessage(ocarinaMemory, ocarinaGeometry, &message, &ocarinaError) &&
              message == 0x9B5U, "song title must use the native message table");
  auto hiddenTitle = ocarinaGeometry;
  hiddenTitle.SongLearned = false;
  Require(ReadTopScreenOcarinaSongMessage(ocarinaMemory, hiddenTitle, &message) && message == 0,
          "unknown song leaked its name");
  hiddenTitle = ocarinaGeometry;
  hiddenTitle.SongSelected = false;
  Require(ReadTopScreenOcarinaSongMessage(ocarinaMemory, hiddenTitle, &message) && message == 0,
          "free play without selection leaked a title");

  constexpr std::uint32_t overlay = 0x00560000;
  const auto floats = [&](std::uint32_t address, const auto &values) {
    return ocarinaMemory.WriteBytes(address, std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t *>(values.data()), sizeof(values)));
  };
  for (std::uint32_t layer = 0; layer < 2; ++layer) {
    const auto model = overlay + 0x100 + layer * 0x200;
    const auto descriptor = overlay + 0x600 + layer * 0x100;
    const auto buffer = overlay + 0x900 + layer * 0x200;
    const auto texture = overlay + 0xE00 + layer * 0x80;
    const std::array<float, 12> positions{100, 30, 0, 100, 46, 0, 110, 30, 0, 110, 46, 0};
    const std::array<float, 8> uv{0.25F, 0.75F, 0.25F, 0.5F, 0.5F, 0.75F, 0.5F, 0.5F};
    const std::array<float, 16> colors{0, 0, 0, 0.8F, 0, 0, 0, 0.8F,
                                       0, 0, 0, 0.8F, 0, 0, 0, 0.8F};
    Require(ocarinaMemory.Write32(overlay + 8 + layer * 4, model) &&
                ocarinaMemory.Write32(model, descriptor) &&
                ocarinaMemory.Write32(descriptor + 0xC, 4) &&
                ocarinaMemory.Write32(descriptor + 0x1C, 0xC0) &&
                ocarinaMemory.Write32(model + 0x128, 1) &&
                ocarinaMemory.Write32(model + 0x1A0, buffer) &&
                ocarinaMemory.Write32(overlay + layer * 4, texture) &&
                ocarinaMemory.Write32(texture + 0x4C, 0x18000000 + layer * 0x1000) &&
                floats(buffer, positions) && floats(buffer + 48, uv) && floats(buffer + 80, colors),
            "could not seed native generated-text models");
  }
  std::vector<oot3d::ui::UiPrimitive> titlePrimitives;
  Require(ReadTopScreenOcarinaTextPrimitives(ocarinaMemory, overlay, ocarinaGeometry,
                                            titlePrimitives, &ocarinaError) && titlePrimitives.size() == 2 &&
              titlePrimitives[0].destination.x == 140 && titlePrimitives[0].destination.y == 30 &&
              titlePrimitives[0].destination.width == 10 && titlePrimitives[0].destination.height == 16 &&
              titlePrimitives[0].uv.y == 0.25F && titlePrimitives[0].uv.height == 0.25F &&
              titlePrimitives[0].color.red == 0 && titlePrimitives[1].color.red == 110.0F / 255 &&
              titlePrimitives[1].color.alpha == 0.8F,
          "native title import lost geometry, orientation, shadow or song tint");
  Require(ocarinaMemory.Write32(overlay + 0x700 + 0xC, 4097) &&
              !ReadTopScreenOcarinaTextPrimitives(ocarinaMemory, overlay, ocarinaGeometry,
                                                  titlePrimitives, &ocarinaError) &&
              titlePrimitives.size() == 2, "malformed model appended a partial title");
  Require(
      std::abs(ocarinaGeometry.Quads[0].Position.X - 50.0F) < 0.001F &&
          std::abs(ocarinaGeometry.Quads[0].Position.Y - 189.0F) < 0.001F &&
          std::abs(ocarinaGeometry.Quads[0].Size.Y - 39.0F) < 0.001F &&
          std::abs(ocarinaGeometry.Quads[0].AtlasOrigin.Y - 81.0F) < 0.001F &&
          std::abs(ocarinaGeometry.Quads[0].AtlasSize.Y - 59.0F) < 0.001F,
      "ocarina base-quad transformation is incorrect");
  Require(
      ocarinaGeometry.Quads[6].Visible && ocarinaGeometry.Quads[7].Visible &&
          !ocarinaGeometry.Quads[8].Visible &&
          std::abs(ocarinaGeometry.Quads[6].Position.X - 122.0F) < 0.001F &&
          std::abs(ocarinaGeometry.Quads[6].Position.Y - 174.0F) < 0.001F &&
          ocarinaGeometry.Quads[14].Visible &&
          std::abs(ocarinaGeometry.Quads[14].Size.X - 24.0F) < 0.001F,
      "ocarina marker or arrow reconstruction is incorrect");
  std::vector<oot3d::ui::UiPrimitive> ocarinaPresentation;
  const oot3d::ui::UiTextureIdentity ocarinaTexture{
      0x005D0000U, 0x18000000U, "oot3d/native/pause_shared/ocarina_page"};
  const oot3d::ui::UiTextureIdentity itemIcons{
      0x005D1000U, 0x18001000U, "oot3d/native/pause_shared/item_icons"};
  Require(AppendTopScreenOcarinaPresentation(ocarinaGeometry, ocarinaTexture,
                                              ocarinaPresentation, itemIcons) == 10U &&
              ocarinaPresentation.front().subsystem ==
                  oot3d::ui::UiSubsystem::TouchControls &&
              ocarinaPresentation.front().role ==
                  oot3d::ui::UiPrimitiveRole::TouchControl &&
              std::abs(ocarinaPresentation.front().color.alpha - 0.68F) <
                  0.001F,
          "ocarina presentation did not preserve recovered semantics");
  Require(ocarinaMemory.Write32(0x005093E4U + 0x38U, 1U) &&
              ReadTopScreenOcarinaGeometry(ocarinaMemory, ocarinaBrowser, &ocarinaGeometry,
                                            &ocarinaError) &&
              !ocarinaGeometry.Active,
          "transitioning ocarina owner still produced a presentation");
  ocarinaMemory = BuildOcarinaFixture();
  Require(ocarinaMemory.Write32(0x0050AF68U, 0U) &&
              ReadTopScreenOcarinaGeometry(ocarinaMemory, ocarinaBrowser, &ocarinaGeometry,
                                            &ocarinaError) &&
              ocarinaGeometry.Active,
          "ocarina presentation incorrectly requires a paused game");
  Require(ocarinaGeometry.Quads[3].Position.X == 85.0F &&
              ocarinaGeometry.Quads[3].Position.Y == -127.0F &&
              ocarinaGeometry.Quads[0].Color.red == 1.0F &&
              ocarinaGeometry.Quads[6].Color.red == 1.0F,
          "ocarina band source indices or white tint regressed");
  Require(ocarinaPresentation[0].uv.y == 81.0F / 256.0F &&
              ocarinaPresentation[0].uv.height > 0,
          "native PICA V inversion leaked into host UI primitives");
  Require(ocarinaMemory.Write32(0x00587A14U, 0) &&
              ReadTopScreenOcarinaGeometry(ocarinaMemory, ocarinaBrowser,
                                           &ocarinaGeometry, &ocarinaError) &&
              !ocarinaGeometry.SongLearned && ocarinaGeometry.UnknownSong.Visible &&
              !ocarinaGeometry.Quads[6].Visible,
          "unlearned song must show the mod marker without revealing notes");
  std::vector<oot3d::ui::UiPrimitive> unknownPresentation;
  AppendTopScreenOcarinaPresentation(ocarinaGeometry, ocarinaTexture,
                                      unknownPresentation, itemIcons);
  const auto &unknown = unknownPresentation.back();
  Require(unknown.source_quad == 16 && unknown.layer == 21 &&
              unknown.texture.semantic_name == itemIcons.semantic_name &&
              unknown.texture.guest_resource_address == itemIcons.guest_resource_address &&
              unknown.uv.x == 406.0F / 512 && unknown.uv.y == 484.0F / 512 &&
              unknown.uv.width == 54.0F / 512 && unknown.uv.height == 24.0F / 512,
          "unknown-song marker must use native ItemIcons, not the custom menu");

  // Notes played into the staff of the game's ocarina box: the song menu
  // yields (no graphics, no browsing, no guide toggle) until the game
  // empties the staff again. Free play draws no box and keeps the menu.
  {
    auto staffMemory = BuildOcarinaFixture();
    TopScreenOcarinaBrowser staffBrowser;
    TopScreenOcarinaState staffState;
    TopScreenOcarinaGeometry staffGeometry;
    const auto menuShown = [&] {
      return ReadTopScreenOcarinaState(staffMemory, &staffState) &&
             staffState.Active && !staffState.StaffNotes &&
             ReadTopScreenOcarinaGeometry(staffMemory, staffBrowser,
                                          &staffGeometry, &ocarinaError) &&
             staffGeometry.Active && staffGeometry.Quads[6].Visible;
    };
    const auto menuYields = [&] {
      return ReadTopScreenOcarinaState(staffMemory, &staffState) &&
             staffState.Active && staffState.StaffNotes &&
             ReadTopScreenOcarinaGeometry(staffMemory, staffBrowser,
                                          &staffGeometry, &ocarinaError) &&
             !staffGeometry.Active;
    };
    Require(menuShown(), "free play with an empty staff must show the song menu");
    Require(staffMemory.Write32(kStaffNoteSlots, 0U) &&
                staffMemory.Write32(kStaffNoteSlots + 7U * 8U, 4U) && menuShown(),
            "free play hid the song menu for notes the game draws in no box");
    Require(staffMemory.Write32(kStaffNoteSlots, 0xFFU) &&
                staffMemory.Write32(kStaffNoteSlots + 7U * 8U, 0xFFU) &&
                staffMemory.Write8(kMessageContext + 0xDU, 1U) && menuShown(),
            "the game's ocarina box with an empty staff must show the song menu");
    // Button 0 is a real note; 0xFF alone marks an empty slot.
    for (const auto [slot, note] : {std::pair{0U, 0U}, std::pair{7U, 4U}}) {
      Require(staffMemory.Write32(kStaffNoteSlots + slot * 8U, note) && menuYields(),
              "the song menu overlapped notes played into the game's ocarina box");
      Require(staffMemory.Write32(kStaffNoteSlots + slot * 8U, 0xFFU),
              "could not empty the ocarina staff");
    }
    // 0x0042CBCC draws no box in states 0, 1, 0xE, 0xF or without +0xC/+0xE.
    Require(staffMemory.Write32(kStaffNoteSlots, 2U) && menuYields(),
            "the song menu overlapped a note in the game's ocarina box");
    for (const std::uint8_t boxState : {0, 1, 0xE, 0xF}) {
      Require(staffMemory.Write8(kMessageContext + 0xF38U, boxState) && menuShown(),
              "the song menu yielded to a box the game does not draw");
    }
    Require(staffMemory.Write8(kMessageContext + 0xF38U, 3U) &&
                staffMemory.Write8(kMessageContext + 0xEU, 0U) && menuShown() &&
                staffMemory.Write8(kMessageContext + 0xEU, 1U) &&
                staffMemory.Write8(kMessageContext + 0xCU, 0U) && menuShown() &&
                staffMemory.Write8(kMessageContext + 0xCU, 1U) && menuYields() &&
                staffMemory.Write32(kStaffNoteSlots, 0xFFU),
            "the song menu ignored the native box draw gate");
    const TopScreenOcarinaState playing{true, 12, 0, true};
    staffBrowser.Advance(playing, false, true, false, true, true);
    Require(staffBrowser.SelectedSong() == -2 && !staffBrowser.GuideHidden() &&
                staffBrowser.TakeGuideSound() == 0U,
            "the hidden song menu browsed or toggled while notes were shown");
    staffBrowser.Advance({true, 12, 0}, false, true, false, true);
    Require(staffBrowser.SelectedSong() == 0,
            "the song menu did not browse again after the staff emptied");
    Require(ReadTopScreenOcarinaGeometry(staffMemory, staffBrowser,
                                         &staffGeometry, &ocarinaError) &&
                staffGeometry.Active,
            "the song menu did not return when the game emptied its staff");
    Require(staffMemory.Write32(0x005093E4U + 0x38U, 1U) &&
                staffMemory.Write32(kStaffNoteSlots, 2U) &&
                ReadTopScreenOcarinaState(staffMemory, &staffState) &&
                !staffState.Active && !staffState.StaffNotes,
            "an inactive song menu read the game's staff");
  }
  const auto generationBefore = ocarinaMemory.WriteGeneration();
  TopScreenOcarinaState browserState{true, 12, 0};
  ocarinaBrowser.Advance(browserState, false, true);
  Require(ocarinaBrowser.SelectedSong() == 0, "first right must select song zero");
  for (unsigned i = 0; i < 11; ++i) ocarinaBrowser.Advance(browserState, false, true);
  Require(ocarinaBrowser.SelectedSong() == 0, "song repeat occurred before 12 updates");
  ocarinaBrowser.Advance(browserState, false, true);
  Require(ocarinaBrowser.SelectedSong() == 1, "song repeat missing at update 12");
  for (unsigned i = 0; i < 5; ++i) ocarinaBrowser.Advance(browserState, false, true);
  Require(ocarinaBrowser.SelectedSong() == 2, "song repeat missing after five updates");
  ocarinaBrowser.Advance(browserState, true, false);
  Require(ocarinaBrowser.SelectedSong() == 1, "direction reversal must select immediately");
  ocarinaBrowser.Advance({}, false, false);
  ocarinaBrowser.Advance(browserState, true, false);
  Require(ocarinaBrowser.SelectedSong() == 11, "first left must select song eleven");
  ocarinaBrowser.Advance(browserState, true, false, true, false);
  Require(ocarinaBrowser.SelectedSong() == 10,
          "release/repress between native updates must not become held repeat");
  ocarinaBrowser.Reset();
  ocarinaBrowser.Advance(browserState, true, true);
  Require(ocarinaBrowser.SelectedSong() == 0 && ocarinaBrowser.Direction() == 1,
          "right must have priority in both selection and arrow presentation");
  ocarinaBrowser.Advance(browserState, false, false, false, false, true);
  Require(ocarinaBrowser.GuideHidden(), "Up edge must hide the guide");
  Require(ocarinaBrowser.TakeGuideSound() == 0x01000499U &&
          ocarinaBrowser.TakeGuideSound() == 0U,
          "hide sound must be delivered once, not for held Up");
  for (unsigned i = 0; i < 30; ++i)
    ocarinaBrowser.Advance(browserState, false, true);
  Require(ocarinaBrowser.SelectedSong() == 0,
          "hidden guide must not browse or repeat");
  Require(ReadTopScreenOcarinaGeometry(ocarinaMemory, ocarinaBrowser, &ocarinaGeometry) &&
              !ocarinaGeometry.Active, "hidden guide still publishes graphics");
  ocarinaBrowser.Advance({}, false, false);
  Require(ocarinaBrowser.GuideHidden() && ocarinaBrowser.SelectedSong() == -2,
          "closing must clear selection but retain guide visibility preference");
  ocarinaBrowser.Advance({false, 12, 0}, false, false, false, false, true);
  Require(ocarinaBrowser.GuideHidden(), "transition accepted a guide toggle");
  ocarinaBrowser.Advance(browserState, false, false, false, false, true);
  Require(!ocarinaBrowser.GuideHidden(), "second Up edge must restore the guide");
  Require(ocarinaBrowser.TakeGuideSound() == 0x01000498U,
          "show guide must use the original distinct sound");
  ocarinaBrowser.Advance(browserState, false, false, false, false, true);
  ocarinaBrowser.Reset();
  Require(!ocarinaBrowser.GuideHidden(), "runtime reset retained hidden guide state");
  Require(ocarinaBrowser.TakeGuideSound() == 0U, "runtime reset retained a guide sound");
  Require(ocarinaMemory.WriteGeneration() == generationBefore,
          "guide navigation must not modify native pause or quest state");

  const auto idleNavigation = BuildTopScreenOcarinaNavigationGeometry(0);
  const auto leftNavigation = BuildTopScreenOcarinaNavigationGeometry(-1);
  const auto rightNavigation = BuildTopScreenOcarinaNavigationGeometry(1);
  Require(std::abs(idleNavigation.Quads[0].Color.red - 0.6F) < 0.001F &&
              std::abs(idleNavigation.Quads[1].Color.red - 0.6F) < 0.001F &&
              std::abs(idleNavigation.Quads[0].Position.Y - 191.0F) < 0.001F &&
              std::abs(leftNavigation.Quads[0].Color.red - 1.0F) < 0.001F &&
              std::abs(leftNavigation.Quads[0].Position.Y - 193.0F) < 0.001F &&
              std::abs(leftNavigation.Quads[1].Color.red - 0.6F) < 0.001F &&
              std::abs(rightNavigation.Quads[1].Color.red - 1.0F) < 0.001F &&
              std::abs(rightNavigation.Quads[1].Position.Y - 193.0F) < 0.001F &&
              leftNavigation.Quads[0].Size.X == 26.0F &&
              leftNavigation.Quads[0].AtlasSize.X == -26.0F &&
              rightNavigation.Quads[1].Size.X == 26.0F,
          "pause navigation geometry does not match the payload producer");
  std::vector<oot3d::ui::UiPrimitive> navigationPresentation;
  const oot3d::ui::UiTextureIdentity navigationTexture{
      0x005D1000U, 0x18010000U, "oot3d/native/pause_shared/pause_top_page"};
  Require(
      AppendTopScreenOcarinaNavigationPresentation(
          leftNavigation, navigationTexture, navigationPresentation) == 2U &&
          navigationPresentation.front().role ==
              oot3d::ui::UiPrimitiveRole::TouchControl &&
          std::abs(navigationPresentation.front().color.alpha - 0.8F) < 0.001F,
      "pause navigation presentation lost recovered texture semantics");

  std::cout << "TopScreen ocarina contracts passed\n";
}
