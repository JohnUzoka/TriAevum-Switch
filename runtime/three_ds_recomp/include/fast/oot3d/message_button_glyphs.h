#pragma once

// The game's message button icons relabelled for the Switch's triggers.
//
// A message shows a button icon with the control code 7F 24 n: sprite 100 + n
// of message/sprite.qsp, a 16x16 cell of message/parts.ctxb (256x128 RGBA4,
// one level). n = 10 is the 3DS L (Z-target), 11 the R (shield), also two
// ocarina notes: the ocarina staff draws them from sprites 150/151, cells of
// the same atlas. The atlas has no ZL/ZR art and no font is involved, so with
// the game's L/R on the Switch's ZL/ZR (the default bindings) the renderer
// rewrites those four cells when it makes the atlas's texture, native or a
// texture pack image: the cell's own plate and letter stay, the letter
// narrower behind a "Z" drawn in its ink. Nothing derived from the game is
// stored: the cells are rewritten from the pixels at hand each time.

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace Fast::Oot3d {

// Bits of the relabel mask.
inline constexpr uint8_t kMessageGlyphZl = 1U; // the L icon reads ZL
inline constexpr uint8_t kMessageGlyphZr = 2U; // the R icon reads ZR

// Published by the game thread every refresh (from the controller bindings),
// read by the render thread when it makes a texture.
inline std::atomic<uint8_t>& MessageShoulderGlyphsState() noexcept {
    static std::atomic<uint8_t> state{0U};
    return state;
}
inline void SetMessageShoulderGlyphs(uint8_t mask) noexcept {
    MessageShoulderGlyphsState().store(mask & (kMessageGlyphZl | kMessageGlyphZr), std::memory_order_relaxed);
}
inline uint8_t MessageShoulderGlyphs() noexcept {
    return MessageShoulderGlyphsState().load(std::memory_order_relaxed);
}

inline constexpr uint32_t kMessagePartsAtlasWidth = 256U;
inline constexpr uint32_t kMessagePartsAtlasHeight = 128U;
inline constexpr uint8_t kMessagePartsAtlasFormat = 4U; // PICA RGBA4
inline constexpr size_t kMessagePartsAtlasBytes = kMessagePartsAtlasWidth * kMessagePartsAtlasHeight * 2U;
// FNV-1a 64 of the atlas's encoded level (EUR and USA RomFS: the same file).
inline constexpr uint64_t kMessagePartsAtlasFnv1a = 0x1A6286E6EC1FF273ULL;

inline constexpr bool IsMessagePartsAtlasShape(uint8_t format, uint32_t width, uint32_t height) noexcept {
    return format == kMessagePartsAtlasFormat && width == kMessagePartsAtlasWidth &&
           height == kMessagePartsAtlasHeight;
}

inline uint64_t MessagePartsFnv1a(std::span<const uint8_t> bytes) noexcept {
    uint64_t hash = 0xCBF29CE484222325ULL;
    for (const uint8_t byte : bytes) {
        hash ^= byte;
        hash *= 0x100000001B3ULL;
    }
    return hash;
}

// The game's own atlas (the encoded bytes as the game uploads them).
inline bool IsMessagePartsAtlas(uint8_t format, uint32_t width, uint32_t height,
                                std::span<const uint8_t> nativeBytes) noexcept {
    return IsMessagePartsAtlasShape(format, width, height) && nativeBytes.size() >= kMessagePartsAtlasBytes &&
           MessagePartsFnv1a(nativeBytes.first(kMessagePartsAtlasBytes)) == kMessagePartsAtlasFnv1a;
}

// Mixed into a texture cache key so a change of the bindings makes the atlas
// again: 0 unless something is relabelled and the texture has the atlas's
// shape (the content is checked when the texture is made).
inline constexpr uint64_t MessagePartsAtlasKeySalt(uint8_t format, uint32_t width, uint32_t height,
                                                   uint8_t mask) noexcept {
    return mask == 0U || !IsMessagePartsAtlasShape(format, width, height)
               ? 0U
               : 0x4D53475A4C5A5200ULL ^ mask; // "MSGZLZR" + mask
}

struct MessageShoulderCell {
    uint32_t X = 0U; // top-left in the 256x128 atlas, rows top first (as decoded)
    uint32_t Y = 0U;
    uint8_t Glyph = 0U; // kMessageGlyphZl or kMessageGlyphZr
};
// Message icons 10/11 (sprites 110/111) and the staff notes (sprites 150/151).
inline constexpr std::array<MessageShoulderCell, 4> kMessageShoulderCells{{
    {192U, 32U, kMessageGlyphZl},
    {208U, 32U, kMessageGlyphZr},
    {0U, 48U, kMessageGlyphZl},
    {16U, 48U, kMessageGlyphZr},
}};

namespace detail {

inline float MessageGlyphLuma(const uint8_t* pixel) noexcept {
    return (77.0F * pixel[0] + 150.0F * pixel[1] + 29.0F * pixel[2]) / 256.0F;
}

// One cell, n x n pixels at `src`/`dst` (rows `stride` bytes apart): a plate
// with one dark letter becomes the plate with "Z" + the letter. False (dst
// untouched) when the cell does not look like that.
inline bool RewriteShoulderCell(const uint8_t* src, uint8_t* dst, size_t stride, uint32_t n) {
    if (n < 16U) {
        return false; // a mip: too small to hold two letters
    }
    const float s = static_cast<float>(n) / 16.0F;
    const auto at = [&](uint32_t x, uint32_t y) { return src + y * stride + x * 4U; };
    // The plate's columns: the opaque run through the middle of the middle
    // row (a pack image's neighbouring cell can bleed into the edge columns).
    const uint32_t mid = n / 2U;
    if (at(mid, mid)[3] < 128U) {
        return false;
    }
    int plateX0 = static_cast<int>(mid);
    int plateX1 = static_cast<int>(mid) + 1;
    while (plateX0 > 0 && at(static_cast<uint32_t>(plateX0 - 1), mid)[3] >= 128U) --plateX0;
    while (plateX1 < static_cast<int>(n) && at(static_cast<uint32_t>(plateX1), mid)[3] >= 128U) ++plateX1;
    const float plateCenter = (plateX0 + plateX1) * 0.5F;
    const float plateSpan = static_cast<float>(plateX1 - plateX0);
    // The letter is in the middle of the plate (3/16 to 13/16 of the cell).
    const uint32_t i0 = static_cast<uint32_t>(std::lround(3.0F * s));
    const uint32_t i1 = static_cast<uint32_t>(std::lround(13.0F * s));
    std::vector<float> lumas;
    lumas.reserve(static_cast<size_t>(i1 - i0) * (i1 - i0));
    for (uint32_t y = i0; y < i1; ++y) {
        for (uint32_t x = i0; x < i1; ++x) {
            lumas.push_back(MessageGlyphLuma(at(x, y)));
        }
    }
    const auto percentile = [&](float p) {
        auto nth = lumas.begin() + static_cast<std::ptrdiff_t>(p * static_cast<float>(lumas.size() - 1U));
        std::nth_element(lumas.begin(), nth, lumas.end());
        return *nth;
    };
    const float plate = percentile(0.75F);
    const float ink = percentile(0.03F);
    if (plate - ink < 32.0F) {
        return false; // no letter darker than its plate
    }
    // Letter coverage: 0 on the plate, 1 at its darkest.
    std::vector<float> cover(static_cast<size_t>(n) * n, 0.0F);
    const auto coverAt = [&](int x, int y) {
        return x < 0 || y < 0 || x >= static_cast<int>(n) || y >= static_cast<int>(n)
                   ? 0.0F
                   : cover[static_cast<size_t>(y) * n + static_cast<size_t>(x)];
    };
    int bx0 = static_cast<int>(n), by0 = static_cast<int>(n), bx1 = -1, by1 = -1;
    for (uint32_t y = i0; y < i1; ++y) {
        for (uint32_t x = i0; x < i1; ++x) {
            const float value = std::clamp((plate - MessageGlyphLuma(at(x, y))) / (plate - ink), 0.0F, 1.0F);
            cover[static_cast<size_t>(y) * n + x] = value;
            if (value > 0.5F) {
                bx0 = std::min(bx0, static_cast<int>(x));
                by0 = std::min(by0, static_cast<int>(y));
                bx1 = std::max(bx1, static_cast<int>(x) + 1);
                by1 = std::max(by1, static_cast<int>(y) + 1);
            }
        }
    }
    if (bx1 <= bx0 || by1 <= by0) {
        return false;
    }
    const float letterWidth = static_cast<float>(bx1 - bx0);
    const float letterHeight = static_cast<float>(by1 - by0);
    // Stroke width: the left stem (L and R both have one), middle half rows.
    std::vector<float> stems;
    for (int y = by0 + (by1 - by0) / 4; y < by1 - (by1 - by0) / 4; ++y) {
        float run = coverAt(bx0 - 1, y);
        for (int x = bx0; x < bx1 && coverAt(x, y) >= 0.25F; ++x) {
            run += coverAt(x, y);
        }
        stems.push_back(run);
    }
    if (stems.empty()) {
        return false;
    }
    std::nth_element(stems.begin(), stems.begin() + static_cast<std::ptrdiff_t>(stems.size() / 2U), stems.end());
    const float stroke = std::max(1.0F, stems[stems.size() / 2U]);
    // The ink: the letter's solid pixels.
    std::array<float, 3> inkColor{};
    float inkCount = 0.0F;
    for (int y = by0; y < by1; ++y) {
        for (int x = bx0; x < bx1; ++x) {
            if (coverAt(x, y) > 0.9F) {
                const uint8_t* pixel = at(static_cast<uint32_t>(x), static_cast<uint32_t>(y));
                for (size_t c = 0U; c < 3U; ++c) inkColor[c] += pixel[c];
                inkCount += 1.0F;
            }
        }
    }
    if (inkCount == 0.0F) {
        return false;
    }
    for (float& channel : inkColor) channel /= inkCount;
    // "Z" + the letter, each `glyphWidth` wide, centred on the plate.
    const float gap = std::max(0.75F * s, 0.5F * stroke);
    const float glyphWidth = std::min(letterWidth, (0.72F * plateSpan - gap) * 0.5F);
    if (glyphWidth < 2.0F) {
        return false;
    }
    const float textX0 = plateCenter - (2.0F * glyphWidth + gap) * 0.5F;
    const float letterX0 = textX0 + glyphWidth + gap;
    const int margin = static_cast<int>(std::ceil(s));
    const int fillX0 = std::max(plateX0 + 1, static_cast<int>(std::floor(std::min(textX0, static_cast<float>(bx0)))) - margin);
    const int fillX1 = std::min(plateX1 - 1,
                                static_cast<int>(std::ceil(std::max(textX0 + 2.0F * glyphWidth + gap,
                                                                    static_cast<float>(bx1)))) + margin);
    std::vector<uint8_t> cell(static_cast<size_t>(n) * n * 4U);
    for (uint32_t y = 0U; y < n; ++y) {
        std::copy_n(at(0U, y), n * 4U, cell.data() + static_cast<size_t>(y) * n * 4U);
    }
    const auto out = [&](int x, int y) { return cell.data() + (static_cast<size_t>(y) * n + static_cast<size_t>(x)) * 4U; };
    // The old letter goes: each row takes its plate colour (median of its
    // plate pixels) over the text area.
    std::vector<uint8_t> channel;
    for (int y = std::max(0, by0 - margin); y < std::min(static_cast<int>(n), by1 + margin); ++y) {
        std::array<uint8_t, 3> median{};
        for (size_t c = 0U; c < 3U; ++c) {
            channel.clear();
            for (uint32_t x = i0; x < i1; ++x) {
                if (coverAt(static_cast<int>(x), y) < 0.15F) channel.push_back(at(x, static_cast<uint32_t>(y))[c]);
            }
            if (channel.empty()) break;
            std::nth_element(channel.begin(), channel.begin() + static_cast<std::ptrdiff_t>(channel.size() / 2U),
                             channel.end());
            median[c] = channel[channel.size() / 2U];
        }
        if (channel.empty()) continue;
        for (int x = fillX0; x < fillX1; ++x) {
            std::copy_n(median.data(), 3U, out(x, y));
        }
    }
    // The letter, narrower: its outer `edge` columns (stem, bowl, bar end)
    // keep their width, the middle is squeezed.
    const float edge = std::min(1.3F * stroke, glyphWidth * 0.5F);
    const auto letterSourceX = [&](float u) {
        if (u < edge) return u;
        if (u > glyphWidth - edge) return letterWidth - (glyphWidth - u);
        return edge + (u - edge) * (letterWidth - 2.0F * edge) / std::max(1e-3F, glyphWidth - 2.0F * edge);
    };
    const auto letterCover = [&](float x, int y) {
        const float sx = x - 0.5F;
        const int xi = static_cast<int>(std::floor(sx));
        const float f = sx - static_cast<float>(xi);
        return coverAt(xi, y) * (1.0F - f) + coverAt(xi + 1, y) * f;
    };
    // The Z: two bars and the diagonal from the top bar's right end to the
    // bottom bar's left end, all `stroke` thick.
    const float diagonalX = -glyphWidth;
    const float diagonalY = letterHeight - 2.0F * stroke;
    const float diagonalLength = std::hypot(diagonalX, diagonalY);
    const auto zCover = [&](float u, float v) {
        if (u < 0.0F || u >= glyphWidth || v < 0.0F || v >= letterHeight) return 0.0F;
        if (v < stroke || v >= letterHeight - stroke) return 1.0F;
        const float distance = std::fabs((u - glyphWidth) * diagonalY - (v - stroke) * diagonalX) / diagonalLength;
        return distance <= stroke * 0.55F ? 1.0F : 0.0F;
    };
    constexpr int kSamples = 4;
    for (int y = by0; y < by1; ++y) {
        for (int x = fillX0; x < fillX1; ++x) {
            float coverage = 0.0F;
            for (int j = 0; j < kSamples; ++j) {
                for (int i = 0; i < kSamples; ++i) {
                    const float px = static_cast<float>(x) + (static_cast<float>(i) + 0.5F) / kSamples;
                    const float py = static_cast<float>(y) + (static_cast<float>(j) + 0.5F) / kSamples;
                    const float z = zCover(px - textX0, py - static_cast<float>(by0));
                    const float u = px - letterX0;
                    const float letter = u >= 0.0F && u < glyphWidth
                                             ? letterCover(static_cast<float>(bx0) + letterSourceX(u), y)
                                             : 0.0F;
                    coverage += std::max(z, letter);
                }
            }
            coverage /= static_cast<float>(kSamples * kSamples);
            if (coverage <= 0.0F) continue;
            uint8_t* pixel = out(x, y);
            for (size_t c = 0U; c < 3U; ++c) {
                pixel[c] = static_cast<uint8_t>(
                    std::lround(static_cast<float>(pixel[c]) * (1.0F - coverage) + inkColor[c] * coverage));
            }
        }
    }
    for (uint32_t y = 0U; y < n; ++y) {
        std::copy_n(cell.data() + static_cast<size_t>(y) * n * 4U, n * 4U, dst + y * stride);
    }
    return true;
}

} // namespace detail

// Rewrites the selected cells of the atlas image `src` (RGBA8, width x height,
// rows top first: the native decode or a pack image, any size of the same
// 2:1 shape) into `dst`, a copy of it. Returns the cells rewritten.
inline uint32_t RewriteMessageShoulderGlyphs(uint8_t mask, std::span<const uint8_t> src, std::span<uint8_t> dst,
                                             uint32_t width, uint32_t height) {
    const size_t bytes = static_cast<size_t>(width) * height * 4U;
    if (mask == 0U || src.size() < bytes || dst.size() < bytes ||
        static_cast<uint64_t>(width) * kMessagePartsAtlasHeight !=
            static_cast<uint64_t>(height) * kMessagePartsAtlasWidth) {
        return 0U;
    }
    const uint32_t cell = width * 16U / kMessagePartsAtlasWidth;
    uint32_t rewritten = 0U;
    for (const auto& target : kMessageShoulderCells) {
        if ((mask & target.Glyph) == 0U) continue;
        const uint32_t x = target.X * width / kMessagePartsAtlasWidth;
        const uint32_t y = target.Y * height / kMessagePartsAtlasHeight;
        if (x + cell > width || y + cell > height) continue;
        const size_t offset = (static_cast<size_t>(y) * width + x) * 4U;
        rewritten += detail::RewriteShoulderCell(src.data() + offset, dst.data() + offset, width * 4U, cell) ? 1U : 0U;
    }
    return rewritten;
}

} // namespace Fast::Oot3d
