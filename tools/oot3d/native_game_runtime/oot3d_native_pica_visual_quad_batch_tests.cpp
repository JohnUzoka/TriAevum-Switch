// Water ripples blinking at 60 FPS (0.2.5-test.4, logs48, clips 2026111314590300 and 2026111315004400).
//
// The game draws its EffectSs effects (ripples, splashes, dust, sparks, rain and snow, light glows) as one
// quad batch per effect type, rebuilt on the CPU every frame (RendererQuad, EUR 0x00332A14 / 0x003FC2F8):
// four corners per instance, already transformed, sorted by depth, six indices per quad. Each ripple is two
// instances at the same centre, one turned half a turn (the view from under the water). The in-between frame
// blended vertex k of one frame with vertex k of the next, and gave a batch whose quad count changed no
// partner at all. These tests build such batches and check the in-between sample:
//   - re-sorted: each quad blends with its own previous quad, not the one that had its slot;
//   - count changed: a new quad is drawn as it is, an expired one left out, the others blended;
//   - turned copy at the same centre: the facing keeps the pair apart (no collapse to a line);
//   - draws that are not quad lists, and quad batches whose slots stayed put, keep the old blend exactly;
//   - above kOot3dPicaVisualQuadBatchMaximumQuads quads the old slot-wise blend stays.
#include "oot3d_native_pica_visual_frame.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace {

using namespace Oot3dNativeGame;

constexpr uint32_t kTopColor = 0x18000000U;
constexpr uint16_t kStride = 16U;  // position xyz float, colour rgba8

int gFailures = 0;

void Check(bool condition, const std::string& message) {
    std::printf("%s %s\n", condition ? "ok  " : "FAIL", message.c_str());
    if (!condition) ++gFailures;
}

using Vec3 = std::array<float, 3>;
using Corners = std::array<Vec3, 4>;

// A flat ring quad (in the XZ plane, facing -Y like the game's ripple) or its copy turned half a turn about X.
struct Quad {
    Vec3 Center{};
    float Half = 1.0F;
    bool Turned = false;

    Corners Points() const {
        const float x = Center[0], y = Center[1], z = Center[2], h = Half;
        if (!Turned) return {{{x - h, y, z - h}, {x + h, y, z - h}, {x - h, y, z + h}, {x + h, y, z + h}}};
        return {{{x - h, y, z + h}, {x + h, y, z + h}, {x - h, y, z - h}, {x + h, y, z - h}}};
    }
};

std::shared_ptr<const Oot3dPicaTemporalVertexProgram> Program() {
    auto program = std::make_shared<Oot3dPicaTemporalVertexProgram>();
    program->Hooks.Transform.PositionInputRegister = 0;
    return program;
}

struct BatchOptions {
    bool Native = true;   // a native key (CMB renderer); else only the strict identity
    bool Hooked = true;   // the program names the position input; else the lowest float input is taken
};

Oot3dPicaVulkanDrawPlan Batch(const std::vector<Quad>& quads, uint64_t submission, const BatchOptions& options) {
    Oot3dPicaVulkanDrawPlan plan;
    plan.SubmissionId = submission;
    plan.CompositionDomain = Oot3dPicaCompositionDomain::Scene;
    plan.Composition.Layer = Oot3dPicaCompositionLayer::TransparentWorld;
    if (options.Native) plan.Composition.TemporalIdentity = {0, 0x2A0000, 0x2A0100, uint64_t{1} << 32U, 9};
    plan.VertexShader.StateKey = 77;
    plan.FragmentShader.StateKey = 78;
    if (options.Hooked) plan.VertexShader.TemporalProgram = Program();
    plan.State.Framebuffer.ColorPhysicalAddress = kTopColor;
    plan.State.Framebuffer.Width = 240U;
    plan.State.Framebuffer.Height = 400U;
    plan.State.VertexInput.PhysicalBaseAddress = 0x30000000U;
    plan.State.VertexInput.IndexPhysicalAddress = 0x30100000U;
    Oot3dPicaVulkanVertexBinding binding;
    binding.Binding = 0;
    binding.ByteStride = kStride;
    binding.Bytes.resize(quads.size() * 4U * kStride);
    for (size_t q = 0; q < quads.size(); ++q) {
        const auto points = quads[q].Points();
        for (size_t k = 0; k < 4U; ++k) {
            uint8_t* record = binding.Bytes.data() + (q * 4U + k) * kStride;
            std::memcpy(record, points[k].data(), 12U);
            const std::array<uint8_t, 4> colour{255, 255, 255, static_cast<uint8_t>(40 * q)};
            std::memcpy(record + 12U, colour.data(), 4U);
        }
    }
    plan.VertexBindings.push_back(std::move(binding));
    plan.VertexAttributes.push_back({0, 0, Oot3dPicaVertexFormat::Float, 3, 0});
    plan.VertexAttributes.push_back({2, 0, Oot3dPicaVertexFormat::UnsignedByte, 4, 12});
    plan.Indexed = true;
    plan.IndicesAre16Bit = true;
    plan.IndexPhysicalAddress = 0x30100000U;
    for (size_t q = 0; q < quads.size(); ++q) {
        for (const uint32_t corner : {0U, 1U, 2U, 2U, 1U, 3U}) {
            const auto index = static_cast<uint16_t>(q * 4U + corner);
            plan.IndexBytes.push_back(static_cast<uint8_t>(index));
            plan.IndexBytes.push_back(static_cast<uint8_t>(index >> 8U));
        }
    }
    plan.VertexCount = static_cast<uint32_t>(quads.size() * 6U);
    return plan;
}

// A world mesh drawn before the batch, so the frame has more than one draw.
Oot3dPicaVulkanDrawPlan Ground(uint64_t submission) {
    Oot3dPicaVulkanDrawPlan plan;
    plan.SubmissionId = submission;
    plan.CompositionDomain = Oot3dPicaCompositionDomain::Scene;
    plan.Composition.Layer = Oot3dPicaCompositionLayer::OpaqueWorld;
    plan.Composition.TemporalIdentity = {0, 0x100000, 0x100100, 0, 9};
    plan.VertexShader.StateKey = 11;
    plan.FragmentShader.StateKey = 12;
    plan.State.Framebuffer.ColorPhysicalAddress = kTopColor;
    plan.State.Framebuffer.Width = 240U;
    plan.State.Framebuffer.Height = 400U;
    Oot3dPicaVulkanVertexBinding binding;
    binding.Binding = 0;
    binding.ByteStride = 12;
    binding.Bytes.resize(3U * 12U);
    plan.VertexBindings.push_back(std::move(binding));
    plan.VertexAttributes.push_back({0, 0, Oot3dPicaVertexFormat::Float, 3, 0});
    plan.VertexCount = 3;
    return plan;
}

Oot3dPicaVisualFrame Frame(uint64_t sequence, Oot3dPicaVulkanDrawPlan batch) {
    Oot3dPicaVisualFrame frame;
    frame.Sequence = sequence;
    frame.TopTransfer.InputPhysicalAddress = kTopColor;
    frame.Draws.push_back(Ground(sequence * 10U));
    frame.Draws.push_back(std::move(batch));
    return frame;
}

std::span<const uint8_t> SampledBytes(const Oot3dPicaVisualDrawView& view) {
    if (view.HasVertexBindingOverrides) return view.VertexBindings.front().ResolvedBytes();
    return view.BasePlan->VertexBindings.front().ResolvedBytes();
}

Corners QuadOf(std::span<const uint8_t> bytes, size_t quad) {
    Corners corners{};
    for (size_t k = 0; k < 4U; ++k) std::memcpy(corners[k].data(), bytes.data() + (quad * 4U + k) * kStride, 12U);
    return corners;
}

Corners Mix(const Quad& a, const Quad& b, float alpha) {
    const auto pa = a.Points(), pb = b.Points();
    Corners out{};
    for (size_t k = 0; k < 4U; ++k)
        for (size_t i = 0; i < 3U; ++i) out[k][i] = pa[k][i] + (pb[k][i] - pa[k][i]) * alpha;
    return out;
}

bool Near(const Corners& a, const Corners& b) {
    for (size_t k = 0; k < 4U; ++k)
        for (size_t i = 0; i < 3U; ++i)
            if (std::fabs(a[k][i] - b[k][i]) > 1.0e-3F) return false;
    return true;
}

// Area of the triangle of corners 0, 1, 2 (signed along Y): flat quads facing -Y are negative.
float FacingArea(const Corners& c) {
    const Vec3 a{c[1][0] - c[0][0], c[1][1] - c[0][1], c[1][2] - c[0][2]};
    const Vec3 b{c[2][0] - c[0][0], c[2][1] - c[0][1], c[2][2] - c[0][2]};
    return a[2] * b[0] - a[0] * b[2];
}

struct Sampled {
    Oot3dPicaVisualFrameViewSample View;
    Oot3dPicaVisualQuadBatchStats Stats;
    Oot3dPicaVisualFrame Previous, Current;
};

// The window loop's in-between frame (exact step and carry on).
std::unique_ptr<Sampled> InBetween(Oot3dPicaVulkanDrawPlan before, Oot3dPicaVulkanDrawPlan after) {
    auto out = std::make_unique<Sampled>();
    out->Previous = Frame(1, std::move(before));
    out->Current = Frame(2, std::move(after));
    Oot3dPicaPreparedVisualTransition transition;
    transition.SetOptions({.ExactStep = true, .CarryUnmatched = true});
    if (!transition.Prepare(out->Previous, out->Current) ||
        !transition.Sample(out->Previous, out->Current, 0.5F, out->View)) {
        Check(false, "transition prepared and sampled");
        return out;
    }
    out->Stats = transition.QuadBatchStats();
    return out;
}

const Oot3dPicaVisualDrawView* BatchView(const Sampled& sampled) {
    for (const auto& view : sampled.View.Draws)
        if (view.BasePlan == &sampled.Current.Draws[1]) return &view;
    return nullptr;
}

void TestResorted() {
    for (const bool native : {true, false}) {
        const std::string mode = native ? "native key: " : "strict identity: ";
        const Quad a{{0, -1, -50}, 10}, b{{100, -1, -80}, 20};
        const Quad grownA{{0, -1, -50}, 12}, grownB{{100, -1, -80}, 22};
        // The game re-sorted them by depth: B first now.
        auto sampled = InBetween(Batch({a, b}, 5, {.Native = native}), Batch({grownB, grownA}, 15, {.Native = native}));
        const auto* view = BatchView(*sampled);
        if (view == nullptr) {
            Check(false, mode + "re-sorted batch drawn");
            continue;
        }
        const auto bytes = SampledBytes(*view);
        Check(Near(QuadOf(bytes, 0), Mix(b, grownB, 0.5F)), mode + "re-sorted: slot 0 blends B with B, not with A");
        Check(Near(QuadOf(bytes, 1), Mix(a, grownA, 0.5F)), mode + "re-sorted: slot 1 blends A with A");
        Check(sampled->Stats.Batches == 1 && sampled->Stats.Paired == 1 && sampled->Stats.Rematched == 2 &&
                  sampled->Stats.CountChanged == 0 && sampled->Stats.UnmatchedNew == 0 && sampled->Stats.Dropped == 0,
              mode + "re-sorted: counters batches 1, paired 1, rematched 2");
    }
    // The program without a transform hook: the lowest float input is the position.
    const Quad a{{0, -1, -50}, 10}, b{{100, -1, -80}, 20};
    auto sampled = InBetween(Batch({a, b}, 5, {.Hooked = false}), Batch({b, a}, 15, {.Hooked = false}));
    const auto* view = BatchView(*sampled);
    Check(view != nullptr && Near(QuadOf(SampledBytes(*view), 0), b.Points()) &&
              Near(QuadOf(SampledBytes(*view), 1), a.Points()),
          "no transform hook: position taken from the float input, quads stay put");
}

void TestCountChanged() {
    for (const bool native : {true, false}) {
        const std::string mode = native ? "native key: " : "strict identity: ";
        const Quad a{{0, -1, -50}, 10}, b{{100, -1, -80}, 20}, expired{{300, -1, -60}, 15};
        const Quad grownA{{0, -1, -50}, 11}, grownB{{100, -1, -80}, 21};
        const Quad newC{{-200, -1, -70}, 5}, newE{{-400, -1, -90}, 5};
        auto sampled = InBetween(Batch({a, expired, b}, 5, {.Native = native}),
                                 Batch({newC, grownB, grownA, newE}, 15, {.Native = native}));
        const auto* view = BatchView(*sampled);
        if (view == nullptr) {
            Check(false, mode + "count changed: batch drawn");
            continue;
        }
        const auto bytes = SampledBytes(*view);
        Check(bytes.size() == 4U * 4U * kStride, mode + "count changed: the current batch's four quads are drawn");
        Check(Near(QuadOf(bytes, 0), newC.Points()), mode + "count changed: the new quad is drawn as it is");
        Check(Near(QuadOf(bytes, 3), newE.Points()), mode + "count changed: the second new quad too");
        Check(Near(QuadOf(bytes, 1), Mix(b, grownB, 0.5F)), mode + "count changed: B blended with B");
        Check(Near(QuadOf(bytes, 2), Mix(a, grownA, 0.5F)), mode + "count changed: A blended with A");
        Check(sampled->Stats.Paired == 1 && sampled->Stats.CountChanged == 1 && sampled->Stats.Rematched == 2 &&
                  sampled->Stats.UnmatchedNew == 2 && sampled->Stats.Dropped == 1,
              mode + "count changed: counters paired 1, count changed 1, rematched 2, new 2, dropped 1");
    }
}

void TestTurnedCopy() {
    // A ripple: the flat quad and its copy turned half a turn at the same centre. The game's order of the two
    // changed (as when the depth sort reorders); the old blend folded each onto the other: a line, no ring.
    const Quad flat{{20, -1, -60}, 15}, turned{{20, -1, -60}, 15, true};
    const Quad flat2{{20, -1, -60}, 16}, turned2{{20, -1, -60}, 16, true};
    auto sampled = InBetween(Batch({flat, turned}, 5, {}), Batch({turned2, flat2}, 15, {}));
    const auto* view = BatchView(*sampled);
    if (view == nullptr) {
        Check(false, "turned copy: batch drawn");
        return;
    }
    const auto bytes = SampledBytes(*view);
    const auto slot0 = QuadOf(bytes, 0), slot1 = QuadOf(bytes, 1);
    Check(Near(slot0, Mix(turned, turned2, 0.5F)) && Near(slot1, Mix(flat, flat2, 0.5F)),
          "turned copy: each quad blends with its own copy");
    Check(FacingArea(slot0) > 100.0F && FacingArea(slot1) < -100.0F,
          "turned copy: neither collapses, each keeps its facing");
    const auto old = Mix(flat, turned, 0.5F);
    Check(std::fabs(FacingArea(old)) < 1.0e-3F, "turned copy: the old slot-wise blend was a line (the blink)");
}

void TestUnchangedDraws() {
    // A quad batch whose slots stayed put: the old slot-wise blend, bit for bit.
    {
        const Quad a{{0, -1, -50}, 10}, b{{100, -1, -80}, 20};
        const Quad grownA{{0, -1, -50}, 12}, grownB{{100, -1, -80}, 22};
        auto sampled = InBetween(Batch({a, b}, 5, {}), Batch({grownA, grownB}, 15, {}));
        Oot3dPicaVisualFrameSample legacy;
        Check(SampleOot3dPicaVisualFrame(sampled->Previous, sampled->Current, 0.5F, legacy), "legacy sample");
        const auto* view = BatchView(*sampled);
        const auto bytes = view != nullptr ? SampledBytes(*view) : std::span<const uint8_t>{};
        const auto legacyBytes = legacy.Draws.size() == 2U ? legacy.Draws[1].VertexBindings.front().ResolvedBytes()
                                                           : std::span<const uint8_t>{};
        Check(!bytes.empty() && bytes.size() == legacyBytes.size() &&
                  std::memcmp(bytes.data(), legacyBytes.data(), bytes.size()) == 0 &&
                  Near(QuadOf(bytes, 0), Mix(a, grownA, 0.5F)),
              "slots kept: the slot-wise blend, same bytes in the view and the full sample");
        Check(sampled->Stats.Paired == 1 && sampled->Stats.Rematched == 0, "slots kept: nothing rematched");
    }
    // Not a quad list (indices 0..5 over six vertices): the slot-wise blend as before.
    {
        auto before = Batch({{{0, -1, -50}, 10}}, 5, {});
        auto after = Batch({{{0, -1, -50}, 10}}, 15, {});
        for (auto* plan : {&before, &after}) {
            plan->VertexBindings.front().Bytes.resize(6U * kStride);
            plan->IndexBytes.clear();
            for (uint16_t i = 0; i < 6U; ++i) {
                plan->IndexBytes.push_back(static_cast<uint8_t>(i));
                plan->IndexBytes.push_back(0);
            }
        }
        for (size_t v = 0; v < 6U; ++v) {
            const std::array<float, 3> p{static_cast<float>(v), 0, 0}, q{static_cast<float>(v) + 2.0F, 4, 0};
            std::memcpy(before.VertexBindings.front().Bytes.data() + v * kStride, p.data(), 12U);
            std::memcpy(after.VertexBindings.front().Bytes.data() + v * kStride, q.data(), 12U);
        }
        auto sampled = InBetween(std::move(before), std::move(after));
        const auto* view = BatchView(*sampled);
        bool blended = view != nullptr && view->HasVertexBindingOverrides;
        for (size_t v = 0; blended && v < 6U; ++v) {
            std::array<float, 3> p{};
            std::memcpy(p.data(), SampledBytes(*view).data() + v * kStride, 12U);
            blended = std::fabs(p[0] - (static_cast<float>(v) + 1.0F)) < 1.0e-5F && std::fabs(p[1] - 2.0F) < 1.0e-5F;
        }
        Check(blended, "not a quad list: vertex k blended with vertex k as before");
        Check(sampled->Stats.Batches == 0, "not a quad list: no quad batch counted");
    }
}

void TestCap() {
    const auto ring = [](size_t count, bool reversed) {
        std::vector<Quad> quads;
        for (size_t i = 0; i < count; ++i) {
            const size_t at = reversed ? count - 1U - i : i;
            quads.push_back({{static_cast<float>(at) * 50.0F, -1, -60}, 10});
        }
        return quads;
    };
    {
        const size_t count = kOot3dPicaVisualQuadBatchMaximumQuads;
        auto sampled = InBetween(Batch(ring(count, false), 5, {}), Batch(ring(count, true), 15, {}));
        const auto* view = BatchView(*sampled);
        Check(view != nullptr && Near(QuadOf(SampledBytes(*view), 0), ring(count, true)[0].Points()),
              "at the cap (256 quads): reversed batch, each quad stays where it is");
        Check(sampled->Stats.Batches == 1 && sampled->Stats.Rematched == count, "at the cap: paired by shape");
    }
    {
        const size_t count = kOot3dPicaVisualQuadBatchMaximumQuads + 1U;
        const auto before = ring(count, false), after = ring(count, true);
        auto sampled = InBetween(Batch(before, 5, {}), Batch(after, 15, {}));
        const auto* view = BatchView(*sampled);
        Check(view != nullptr && Near(QuadOf(SampledBytes(*view), 0), Mix(before[0], after[0], 0.5F)),
              "above the cap (257 quads): the old slot-wise blend");
        Check(sampled->Stats.Batches == 0, "above the cap: not counted as a quad batch");
    }
}

} // namespace

int main() {
    TestResorted();
    TestCountChanged();
    TestTurnedCopy();
    TestUnchangedDraws();
    TestCap();
    std::printf("%s\n", gFailures == 0 ? "all ok" : "FAILED");
    return gFailures == 0 ? 0 : 1;
}
