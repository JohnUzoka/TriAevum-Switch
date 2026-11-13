// The game's message L/R button icons relabelled ZL/ZR from the controller
// bindings (oot3d_message_button_glyphs.h) and the atlas cells rewritten for
// it (fast/oot3d/message_button_glyphs.h). Synthetic atlases only: plates and
// letters drawn here, no game data.
#include "oot3d_message_button_glyphs.h"

#include "fast/oot3d/message_button_glyphs.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <span>
#include <vector>

namespace {

using namespace Oot3dNativeGame;
using Fast::Oot3d::kMessageGlyphZl;
using Fast::Oot3d::kMessageGlyphZr;

void Require(bool condition, const char* message) {
  if (!condition) {
    std::fprintf(stderr, "FAIL: %s\n", message);
    std::exit(1);
  }
}

void SwapShouldersAndTriggers(NativeControlConfig& config) {
  // What Controls > Bindings > "Swap shoulders / triggers" does.
  ThreeDsRecomp::Input::SwapGamepadSources(config.Bindings, NativeGamepadButton::LeftShoulder,
                                           NativeGamepadButton::LeftTrigger);
  ThreeDsRecomp::Input::SwapGamepadSources(config.Bindings, NativeGamepadButton::RightShoulder,
                                           NativeGamepadButton::RightTrigger);
}

void Bindings() {
  auto config = NativeControlDefaults();
  // Switch: the default L/R are on the SDL shoulders, the buttons labelled ZL/ZR.
  Require(MessageShoulderGlyphsForBindings(config, true) == (kMessageGlyphZl | kMessageGlyphZr),
          "Switch defaults: the message L/R icons read ZL/ZR");
  // Elsewhere a shoulder is the controller's L/R: the game's icons stay.
  Require(MessageShoulderGlyphsForBindings(config, false) == 0U, "desktop defaults keep L/R");
  SwapShouldersAndTriggers(config);
  Require(MessageShoulderGlyphsForBindings(config, true) == 0U, "Switch after the swap: L/R are right");
  Require(MessageShoulderGlyphsForBindings(config, false) == (kMessageGlyphZl | kMessageGlyphZr),
          "desktop after the swap: L/R on the triggers read ZL/ZR");
  SwapShouldersAndTriggers(config);
  // One side moved elsewhere: only the other is relabelled.
  config.Bindings[static_cast<std::size_t>(NativeControlAction::R)].Gamepad = NativeGamepadButton::A;
  Require(MessageShoulderGlyphsForBindings(config, true) == kMessageGlyphZl, "R on A keeps its icon");
  config.Bindings[static_cast<std::size_t>(NativeControlAction::L)].Gamepad = NativeGamepadButton::LeftTrigger;
  Require(MessageShoulderGlyphsForBindings(config, true) == 0U, "L on the Switch's L keeps its icon");
  config = NativeControlDefaults();
  config.ControllerEnabled = false;
  Require(MessageShoulderGlyphsForBindings(config, true) == 0U, "no controller: the game's icons");
  // The item lanes (the SSE's ZL/ZR actions) do not decide the message icons.
  config = NativeControlDefaults();
  config.Bindings[static_cast<std::size_t>(NativeControlAction::Zl)].Gamepad = NativeGamepadButton::LeftShoulder;
  config.Bindings[static_cast<std::size_t>(NativeControlAction::Zr)].Gamepad = NativeGamepadButton::RightShoulder;
  Require(MessageShoulderGlyphsForBindings(config, true) == (kMessageGlyphZl | kMessageGlyphZr),
          "item bindings leave the message icons to L/R");
}

void PublishedMask() {
  Fast::Oot3d::SetMessageShoulderGlyphs(0xFFU);
  Require(Fast::Oot3d::MessageShoulderGlyphs() == (kMessageGlyphZl | kMessageGlyphZr), "mask bits limited");
  Fast::Oot3d::SetMessageShoulderGlyphs(0U);
  Require(Fast::Oot3d::MessageShoulderGlyphs() == 0U, "mask cleared");
}

void Identity() {
  using namespace Fast::Oot3d;
  Require(MessagePartsAtlasKeySalt(4U, 256U, 128U, 0U) == 0U, "no relabel: the native key");
  Require(MessagePartsAtlasKeySalt(5U, 256U, 128U, 3U) == 0U, "another format: the native key");
  Require(MessagePartsAtlasKeySalt(4U, 128U, 128U, 3U) == 0U, "another size: the native key");
  const uint64_t zl = MessagePartsAtlasKeySalt(4U, 256U, 128U, kMessageGlyphZl);
  const uint64_t both = MessagePartsAtlasKeySalt(4U, 256U, 128U, kMessageGlyphZl | kMessageGlyphZr);
  Require(zl != 0U && both != 0U && zl != both, "each relabel is its own texture");
  std::vector<uint8_t> bytes(kMessagePartsAtlasBytes, 0x5AU);
  Require(!IsMessagePartsAtlas(4U, 256U, 128U, bytes), "other content is not the atlas");
  Require(!IsMessagePartsAtlas(4U, 256U, 128U, std::span<const uint8_t>(bytes).first(100U)),
          "truncated bytes are not the atlas");
  Require(MessagePartsFnv1a(std::span<const uint8_t>()) == 0xCBF29CE484222325ULL, "FNV-1a 64 offset basis");
  const uint8_t a = 'a';
  Require(MessagePartsFnv1a(std::span<const uint8_t>(&a, 1U)) == 0xAF63DC4C8601EC8CULL, "FNV-1a 64 of \"a\"");
}

struct Image {
  uint32_t Width = 0;
  uint32_t Height = 0;
  std::vector<uint8_t> Pixels;
  uint8_t* At(uint32_t x, uint32_t y) { return Pixels.data() + (static_cast<size_t>(y) * Width + x) * 4U; }
  const uint8_t* At(uint32_t x, uint32_t y) const {
    return Pixels.data() + (static_cast<size_t>(y) * Width + x) * 4U;
  }
};

// Fills [x0,x1) x [y0,y1) in 16ths of the cell at (cellX, cellY) (atlas units).
void Fill(Image& image, uint32_t cellX, uint32_t cellY, float x0, float y0, float x1, float y1, uint8_t r,
          uint8_t g, uint8_t b, uint8_t a) {
  const float s = static_cast<float>(image.Width) / 256.0F;
  for (uint32_t y = static_cast<uint32_t>((cellY + y0) * s); y < static_cast<uint32_t>((cellY + y1) * s); ++y) {
    for (uint32_t x = static_cast<uint32_t>((cellX + x0) * s); x < static_cast<uint32_t>((cellX + x1) * s); ++x) {
      uint8_t* pixel = image.At(x, y);
      pixel[0] = r;
      pixel[1] = g;
      pixel[2] = b;
      pixel[3] = a;
    }
  }
}

void Plate(Image& image, uint32_t x, uint32_t y, char letter) {
  Fill(image, x, y, 1, 1, 15, 15, 40, 40, 40, 255);    // outline
  Fill(image, x, y, 2, 2, 14, 14, 200, 196, 190, 255); // plate
  Fill(image, x, y, 5, 3, 7, 12, 24, 24, 28, 255);     // stem
  if (letter == 'L') {
    Fill(image, x, y, 5, 10, 11, 12, 24, 24, 28, 255);
  } else if (letter == 'R') {
    Fill(image, x, y, 5, 3, 10, 5, 24, 24, 28, 255);
    Fill(image, x, y, 9, 3, 11, 9, 24, 24, 28, 255);
    Fill(image, x, y, 5, 7, 10, 9, 24, 24, 28, 255);
    Fill(image, x, y, 9, 9, 11, 12, 24, 24, 28, 255);
  }
}

// A 2:1 atlas at `scale` x 256x128: a patterned transparent background, the
// four L/R cells and two other plates (A at the message A icon's cell, an
// empty plate at the I icon's).
Image Atlas(uint32_t scale) {
  Image image{256U * scale, 128U * scale, {}};
  image.Pixels.resize(static_cast<size_t>(image.Width) * image.Height * 4U);
  for (uint32_t y = 0; y < image.Height; ++y) {
    for (uint32_t x = 0; x < image.Width; ++x) {
      uint8_t* pixel = image.At(x, y);
      pixel[0] = static_cast<uint8_t>(x * 7U);
      pixel[1] = static_cast<uint8_t>(y * 13U);
      pixel[2] = static_cast<uint8_t>((x ^ y) * 3U);
      pixel[3] = static_cast<uint8_t>((x + y) % 5U == 0U ? 255U : 0U);
    }
  }
  Plate(image, 192, 32, 'L');
  Plate(image, 208, 32, 'R');
  Plate(image, 0, 48, 'L');
  Plate(image, 16, 48, 'R');
  Plate(image, 128, 32, 'L'); // stands for another icon: never touched
  Plate(image, 192, 48, ' '); // a plate without a letter
  return image;
}

bool InsideCell(const Image& image, uint32_t x, uint32_t y, uint32_t cellX, uint32_t cellY) {
  const uint32_t s = image.Width / 256U;
  return x >= cellX * s && x < (cellX + 16U) * s && y >= cellY * s && y < (cellY + 16U) * s;
}

// Groups of ink columns in a cell's letter rows (dark opaque pixels).
int InkGroups(const Image& image, uint32_t cellX, uint32_t cellY) {
  const uint32_t s = image.Width / 256U;
  int groups = 0;
  bool previous = false;
  for (uint32_t x = cellX * s; x < (cellX + 16U) * s; ++x) {
    bool ink = false;
    for (uint32_t y = (cellY + 3U) * s; y < (cellY + 12U) * s; ++y) {
      const uint8_t* pixel = image.At(x, y);
      ink |= pixel[3] == 255U && pixel[0] < 90U && x >= (cellX + 3U) * s && x < (cellX + 13U) * s;
    }
    groups += ink && !previous ? 1 : 0;
    previous = ink;
  }
  return groups;
}

void CheckRewrite(uint32_t scale) {
  const Image source = Atlas(scale);
  Image result = source;
  const auto rewritten = Fast::Oot3d::RewriteMessageShoulderGlyphs(
      kMessageGlyphZl | kMessageGlyphZr, source.Pixels, result.Pixels, source.Width, source.Height);
  Require(rewritten == 4U, "the two message icons and the two staff notes are rewritten");
  for (uint32_t y = 0; y < source.Height; ++y) {
    for (uint32_t x = 0; x < source.Width; ++x) {
      const bool target = InsideCell(source, x, y, 192, 32) || InsideCell(source, x, y, 208, 32) ||
                          InsideCell(source, x, y, 0, 48) || InsideCell(source, x, y, 16, 48);
      const uint8_t* before = source.At(x, y);
      const uint8_t* after = result.At(x, y);
      if (!target) {
        Require(std::equal(before, before + 4, after), "pixels outside the L/R cells are untouched");
      }
      Require(before[3] == after[3], "the cells keep their alpha (the plate's shape)");
    }
  }
  for (const auto& cell : Fast::Oot3d::kMessageShoulderCells) {
    Require(InkGroups(source, cell.X, cell.Y) == 1, "the synthetic cell has one letter");
    Require(InkGroups(result, cell.X, cell.Y) == 2, "the rewritten cell has two letters: Z and L/R");
    // The Z's top bar starts left of the old letter's stem (5/16).
    const uint32_t s = scale;
    bool zLeft = false;
    for (uint32_t x = (cell.X + 3U) * s; x < (cell.X + 5U) * s; ++x) {
      const uint8_t* pixel = result.At(x, (cell.Y + 3U) * s + s / 2U);
      zLeft |= pixel[0] < 90U;
    }
    Require(zLeft, "the Z stands left of where the letter was");
  }
  Require(InkGroups(result, 128, 32) == 1, "other icons keep their letter");

  // Only ZL: the R cells stay.
  Image left = source;
  Require(Fast::Oot3d::RewriteMessageShoulderGlyphs(kMessageGlyphZl, source.Pixels, left.Pixels, source.Width,
                                                    source.Height) == 2U,
          "ZL alone rewrites the two L cells");
  Require(InkGroups(left, 192, 32) == 2 && InkGroups(left, 208, 32) == 1 && InkGroups(left, 0, 48) == 2 &&
              InkGroups(left, 16, 48) == 1,
          "ZL alone leaves the R cells");
}

void RewriteGuards() {
  const Image source = Atlas(1);
  Image result = source;
  Require(Fast::Oot3d::RewriteMessageShoulderGlyphs(0U, source.Pixels, result.Pixels, 256U, 128U) == 0U &&
              result.Pixels == source.Pixels,
          "mask 0 changes nothing");
  Require(Fast::Oot3d::RewriteMessageShoulderGlyphs(3U, source.Pixels, result.Pixels, 256U, 256U) == 0U,
          "another shape is not the atlas");
  Require(Fast::Oot3d::RewriteMessageShoulderGlyphs(
              3U, source.Pixels, std::span<uint8_t>(result.Pixels).first(100U), 256U, 128U) == 0U,
          "a short destination is refused");
  // A mip (8-pixel cells) is left as it is.
  Image mip{128U, 64U, std::vector<uint8_t>(128U * 64U * 4U, 200U)};
  Image mipResult = mip;
  Require(Fast::Oot3d::RewriteMessageShoulderGlyphs(3U, mip.Pixels, mipResult.Pixels, 128U, 64U) == 0U &&
              mipResult.Pixels == mip.Pixels,
          "cells under 16 pixels are left");
  // A cell without a letter is left (here: the L cell emptied).
  Image blank = source;
  Fill(blank, 192, 32, 2, 2, 14, 14, 200, 196, 190, 255);
  Image blankResult = blank;
  Require(Fast::Oot3d::RewriteMessageShoulderGlyphs(kMessageGlyphZl, blank.Pixels, blankResult.Pixels, 256U,
                                                    128U) == 1U,
          "only the L cell that has a letter is rewritten");
  for (uint32_t y = 32U; y < 48U; ++y) {
    for (uint32_t x = 192U; x < 208U; ++x) {
      Require(std::equal(blank.At(x, y), blank.At(x, y) + 4, blankResult.At(x, y)), "an empty plate stays");
    }
  }
}

} // namespace

int main() {
  Bindings();
  PublishedMask();
  Identity();
  CheckRewrite(1U); // the native atlas
  CheckRewrite(3U); // a texture pack image at the 3x cap
  CheckRewrite(4U);
  RewriteGuards();
  std::puts("oot3d_message_button_glyphs_tests: ok");
  return 0;
}
