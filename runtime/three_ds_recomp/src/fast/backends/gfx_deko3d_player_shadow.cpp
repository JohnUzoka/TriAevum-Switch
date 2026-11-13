// config/simple_shadow on deko3d (fast/oot3d/player_shadow.h; SWITCH_PORT.md,
// "Simple shadow"): the opaque draws of Link's actor in a frame are drawn
// again with his own PICA vertex program, their view rows patched to flatten
// him along the scene's light and their projection rows to place him in a
// silhouette of the floor region. At the frame's overlay point (or before a
// copy reads the target), one triangle over the screen rectangle of the
// region's box reads the scene's depth, takes each pixel back to its world
// point, and darkens it by the silhouette where the light takes that point
// (player_shadow_decal_fsh.glsl): the shadow lies on the ground, steps and
// walls that are there and ends at a ledge. Taken only from draws that leave
// depth to the rasterizer; dkperf shadow_decal_ms.
#include "fast/backends/gfx_deko3d.h"
#if defined(__SWITCH__)
#include "fast/backends/gfx_deko3d_pica_pipeline.h"
#include "fast/oot3d/pica_uniform_layout.h"
#include "fast/oot3d/player_shadow.h"
#include "gfx_deko3d_api.h"
#include "gfx_deko3d_player_shadow_dksh.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <limits>
#include <span>
#include <utility>

namespace Fast {
namespace {
using namespace Fast::Deko3d;
// Keys of the built-in stages in the ShaderStore (not text hashes).
constexpr uint64_t kPlayerShadowFragmentKey = 0xDE0C3D5AD0500001ULL;
constexpr uint64_t kPlayerShadowCoverVertexKey = 0xDE0C3D5AD0500002ULL;
constexpr uint64_t kPlayerShadowDecalFragmentKey = 0xDE0C3D5AD0500005ULL;
// The silhouette's side, in texels.
constexpr uint32_t kPlayerShadowSilhouetteSize = 256U;
// A summary line in deko3d.txt every this many shadow frames.
constexpr uint64_t kPlayerShadowReportFrames = 1800U;

// player_shadow_decal_fsh.glsl: binding 0 of its fragment stage (std140).
struct PlayerShadowDecalUniforms {
    float ClipToWorld[16]; // column-major (GLSL mat4)
    float Viewport[4];
    float Origin[4];       // w: 1 when NDC y = -1 is the viewport's first row
    float Normal[4];       // w: toward light . normal
    float TowardLight[4];  // w: the cosine a raised floor faces the normal within
    float Along[4];        // w: first along
    float Across[4];       // w: first across
    float Extent[4];       // 1 / along length, 1 / across length, lowest, highest
    float Link[4];         // w: his radius
    float Middle[4];       // w: his height
    float Shade[4];        // opacity, one texel, the fade's length, the raised floor's height
    float Eye[4];          // the camera; w: the translucent floor's height (none: lowest float)
    float FloorBox[4];     // the translucent floor's lowest x, highest x, lowest z, highest z
};
static_assert(sizeof(PlayerShadowDecalUniforms) == 256U, "the std140 block PlayerShadowDecal");

// The pixels both rectangles hold (none: zero width).
DkScissor Intersect(const DkScissor& a, const DkScissor& b) {
    const uint32_t x0 = std::max(a.x, b.x);
    const uint32_t y0 = std::max(a.y, b.y);
    const uint32_t x1 = std::min(a.x + a.width, b.x + b.width);
    const uint32_t y1 = std::min(a.y + a.height, b.y + b.height);
    return x1 > x0 && y1 > y0 ? DkScissor{x0, y0, x1 - x0, y1 - y0} : DkScissor{};
}

// Oot3d::PlayerShadowScreenRect's pixels (x, y, width, height) inside a target.
DkScissor WithinTarget(const std::array<int32_t, 4>& rect, uint32_t width, uint32_t height) {
    const auto clamp = [](int64_t value, uint32_t limit) {
        return static_cast<uint32_t>(std::clamp<int64_t>(value, 0, limit));
    };
    const uint32_t x0 = clamp(rect[0], width);
    const uint32_t y0 = clamp(rect[1], height);
    const uint32_t x1 = clamp(int64_t{rect[0]} + rect[2], width);
    const uint32_t y1 = clamp(int64_t{rect[1]} + rect[3], height);
    return x1 > x0 && y1 > y0 ? DkScissor{x0, y0, x1 - x0, y1 - y0} : DkScissor{};
}
} // namespace

void GfxRenderingAPIDeko3d::InitPlayerShadow() {
    auto& shadow = mPlayerShadow;
    shadow.Fragment =
        mShaders.Embedded(kPlayerShadowFragmentKey, std::span<const uint8_t>(kPlayerShadowFragmentDksh));
    shadow.CoverVertex =
        mShaders.Embedded(kPlayerShadowCoverVertexKey, std::span<const uint8_t>(kPlayerShadowCoverVertexDksh));
    shadow.DecalFragment =
        mShaders.Embedded(kPlayerShadowDecalFragmentKey, std::span<const uint8_t>(kPlayerShadowDecalFragmentDksh));
    // The silhouette: drawn, then sampled (its red channel); uncompressed,
    // its own block.
    DkImageLayoutMaker maker;
    dkImageLayoutMakerDefaults(&maker, mDevice.Handle());
    maker.flags = DkImageFlags_UsageRender;
    maker.format = DkImageFormat_RGBA8_Unorm;
    maker.dimensions[0] = kPlayerShadowSilhouetteSize;
    maker.dimensions[1] = kPlayerShadowSilhouetteSize;
    DkImageLayout layout;
    dkImageLayoutInitialize(&layout, &maker);
    const uint32_t slot = mResources.AllocateImageSlot();
    if (slot < PicaResources::kMaxImages) {
        shadow.SilhouetteMemory = mDevice.MakeBlock(static_cast<uint32_t>(dkImageLayoutGetSize(&layout)),
                                                    DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image,
                                                    "shadow silhouette");
        if (shadow.SilhouetteMemory != nullptr) {
            dkImageInitialize(&shadow.SilhouetteImage, &layout, shadow.SilhouetteMemory, 0U);
            dkImageViewDefaults(&shadow.SilhouetteView, &shadow.SilhouetteImage);
            mResources.WriteImageDescriptor(slot, shadow.SilhouetteImage);
            shadow.SilhouetteSlot = slot;
        } else {
            mResources.FreeImageSlot(slot);
        }
    }
    const bool ready = shadow.Fragment != nullptr && shadow.CoverVertex != nullptr &&
                       shadow.DecalFragment != nullptr && shadow.SilhouetteMemory != nullptr;
    Oot3d::PlayerShadowBridge::Instance().SetRendererReady(ready);
    Log("shadow: silhouette program %s (%zu B), triangle %s, decal %s, silhouette %ux%u %s",
        shadow.Fragment != nullptr ? "ready" : "MISSING", sizeof(kPlayerShadowFragmentDksh),
        shadow.CoverVertex != nullptr ? "ready" : "MISSING", shadow.DecalFragment != nullptr ? "ready" : "MISSING",
        kPlayerShadowSilhouetteSize, kPlayerShadowSilhouetteSize,
        shadow.SilhouetteMemory != nullptr ? "ready" : "MISSING");
}

void GfxRenderingAPIDeko3d::ObservePlayerShadowDraw(const GfxNativePicaDrawView&) {
    auto& shadow = mPlayerShadow;
    const uint64_t frame = mDevice.Frame();
    if (shadow.Frame == frame) return;
    shadow.Frame = frame;
    shadow.Selected = Oot3d::PlayerShadowBridge::Instance().Selected();
    shadow.Projection.reset();
    shadow.Region.reset();
    shadow.WorldToClip.reset();
    shadow.Light.reset();
    shadow.Draws.clear();
}

void GfxRenderingAPIDeko3d::CapturePlayerShadowDraw(const GfxNativePicaDrawView& draw, const PicaProgram& program,
                                                    const PicaTarget& target, const PlayerShadowDraw& bound) {
    auto& shadow = mPlayerShadow;
    if (!shadow.Selected || shadow.Frame != mDevice.Frame()) return;
    const auto& composition = draw.Composition;
    if (!composition.NativeActorGeometry ||
        composition.NativeActorIdentity != Oot3d::PlayerShadowActorIdentity(shadow.Selected->Actor) ||
        draw.CompositionDomain != Renderer3ds::PicaCompositionDomain::Scene ||
        composition.Layer != Renderer3ds::PicaCompositionLayer::OpaqueWorld || composition.NativeProjectedShadow)
        return;
    // His depth must be the rasterizer's (the floor was tested with it; the
    // parametric program writes that same depth), his uniforms the title's
    // block and his camera known.
    const bool rasterDepth = !draw.WBuffering && draw.DepthRange == -1.0F && draw.NearPlane == 0.0F;
    if (!draw.DepthTestEnabled || !draw.DepthWriteEnabled || !rasterDepth || program.Vertex == nullptr ||
        !draw.LightBinding.WorldToView || draw.VertexUniformBytes.size() < Oot3d::kPicaPackedVertexUniformSize) {
        ++shadow.Skipped;
        return;
    }
    if (!shadow.Projection) {
        auto scene = Oot3d::PlayerShadowSceneLightOf(draw.LightBinding.SceneDirectionals.get());
        const double now =
            std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        if (shadow.EasedLight && shadow.EasedLightActor == shadow.Selected->Actor)
            scene.Direction = Oot3d::PlayerShadowEaseLight(*shadow.EasedLight, scene.Direction,
                                                           static_cast<float>(now - shadow.EasedLightTime));
        shadow.EasedLight = scene.Direction;
        shadow.EasedLightTime = now;
        shadow.EasedLightActor = shadow.Selected->Actor;
        const auto light =
            Oot3d::PlayerShadowLightDirection(scene, shadow.Selected->FloorNormal, shadow.Selected->Height);
        shadow.Projection = Oot3d::PlayerShadowProjection(*shadow.Selected, light);
        shadow.Light = light;
        shadow.Region = Oot3d::PlayerShadowFloorRegion(*shadow.Selected, light);
        shadow.WorldToClip = Oot3d::PlayerShadowWorldToClip(draw.VertexUniformBytes, *draw.LightBinding.WorldToView);
    }
    if (!shadow.Region) {
        ++shadow.Skipped;
        return;
    }
    const auto bytes = static_cast<uint32_t>(draw.VertexUniformBytes.size());
    const uint32_t aligned = AlignUp(bytes, DK_UNIFORM_BUF_ALIGNMENT);
    // The block with the flattening view rows and the silhouette's projection rows.
    const auto patched = [&]() -> DkGpuAddr {
        const StreamSpan stream = mDevice.Stream(aligned, DK_UNIFORM_BUF_ALIGNMENT);
        if (stream.Cpu == nullptr) return DK_GPU_ADDR_INVALID;
        auto* cpu = static_cast<uint8_t*>(stream.Cpu);
        std::memcpy(cpu, draw.VertexUniformBytes.data(), bytes);
        std::memset(cpu + bytes, 0, aligned - bytes);
        const std::span<uint8_t> block(cpu, bytes);
        if (!Oot3d::PatchPlayerShadowViewRows(block, *draw.LightBinding.WorldToView, *shadow.Projection) ||
            !Oot3d::PatchPlayerShadowSilhouetteRows(block, *draw.LightBinding.WorldToView, *shadow.Region))
            return DK_GPU_ADDR_INVALID;
        return stream.Gpu;
    };
    PlayerShadowDraw record = bound;
    record.Target = &target;
    record.Vertex = program.Vertex;
    record.SilhouetteUniforms = patched();
    record.UniformBytes = aligned;
    if (record.SilhouetteUniforms == DK_GPU_ADDR_INVALID) {
        ++shadow.Skipped;
        return;
    }
    shadow.Draws.push_back(record);
}

void GfxRenderingAPIDeko3d::DrawPlayerShadowMesh(DkCmdBuf cmd, const PlayerShadowDraw& draw) {
    dkCmdBufBindVtxAttribState(cmd, draw.Attributes.data(), static_cast<uint32_t>(draw.Attributes.size()));
    dkCmdBufBindVtxBufferState(cmd, draw.Buffers.data(), static_cast<uint32_t>(draw.Buffers.size()));
    dkCmdBufBindVtxBuffers(cmd, 0, draw.Extents.data(), static_cast<uint32_t>(draw.Extents.size()));
    if (draw.Indexed) {
        dkCmdBufBindIdxBuffer(cmd, draw.Indices16 ? DkIdxFormat_Uint16 : DkIdxFormat_Uint8, draw.IndexStream);
        dkCmdBufDrawIndexed(cmd, draw.Primitive, draw.IndexCount, 1, 0, draw.BaseVertex, 0);
    } else {
        dkCmdBufDraw(cmd, draw.Primitive, draw.VertexCount, 1, 0, 0);
    }
}

void GfxRenderingAPIDeko3d::DrawPlayerShadowSilhouette(const std::vector<PlayerShadowDraw>& draws) {
    auto& shadow = mPlayerShadow;
    DkCmdBuf cmd = mDevice.Cmd();
    FlushPicaInvalidations();
    // Drawn again in the same frame (a second target): only once the first
    // quad has read it.
    if (shadow.SilhouetteFrame == mDevice.Frame()) dkCmdBufBarrier(cmd, DkBarrier_Fragments, 0);
    shadow.SilhouetteFrame = mDevice.Frame();
    const DkImageView* colors[] = {&shadow.SilhouetteView};
    dkCmdBufBindRenderTargets(cmd, colors, 1, nullptr);
    mBoundTarget = nullptr; // the scene target is bound again after
    constexpr uint32_t size = kPlayerShadowSilhouetteSize;
    const DkScissor whole{0, 0, size, size};
    dkCmdBufSetScissors(cmd, 0, &whole, 1);
    dkCmdBufClearColorFloat(cmd, 0, DkColorMask_RGBA, 0.0F, 0.0F, 0.0F, 0.0F);
    const StreamSpan white = mDevice.Stream(DK_UNIFORM_BUF_ALIGNMENT, DK_UNIFORM_BUF_ALIGNMENT);
    if (white.Cpu != nullptr) {
        const float rgba[4] = {1.0F, 1.0F, 1.0F, 1.0F};
        std::memcpy(white.Cpu, rgba, sizeof(rgba));
        // Every covered texel white: no blend, depth or stencil.
        DkColorState colorState;
        dkColorStateDefaults(&colorState);
        dkCmdBufBindColorState(cmd, &colorState);
        DkColorWriteState writeState;
        dkColorWriteStateDefaults(&writeState);
        dkCmdBufBindColorWriteState(cmd, &writeState);
        DkDepthStencilState depthStencil;
        dkDepthStencilStateDefaults(&depthStencil);
        depthStencil.depthTestEnable = 0;
        depthStencil.depthWriteEnable = 0;
        depthStencil.stencilTestEnable = 0;
        dkCmdBufBindDepthStencilState(cmd, &depthStencil);
        DkRasterizerState rasterizer;
        dkRasterizerStateDefaults(&rasterizer);
        rasterizer.depthClampEnable = 0;
        rasterizer.cullMode = DkFace_None;
        dkCmdBufBindRasterizerState(cmd, &rasterizer);
        dkCmdBufBindUniformBuffer(cmd, DkStage_Fragment, 0, white.Gpu, DK_UNIFORM_BUF_ALIGNMENT);
        ApplyDkViewport(cmd, 0.0F, 0.0F, static_cast<float>(size), static_cast<float>(size));
        // A texel of border stays clear: the sampler clamps to the edge.
        const DkScissor inner{1, 1, size - 2U, size - 2U};
        dkCmdBufSetScissors(cmd, 0, &inner, 1);
        for (const PlayerShadowDraw& draw : draws) {
            const DkShader* shaders[] = {draw.Vertex, shadow.Fragment};
            dkCmdBufBindShaders(cmd, DkStageFlag_Vertex | DkStageFlag_Fragment, shaders, 2);
            dkCmdBufBindUniformBuffer(cmd, DkStage_Vertex, 0, draw.SilhouetteUniforms, draw.UniformBytes);
            DrawPlayerShadowMesh(cmd, draw);
        }
    }
    // Drawn before the floor quad samples it.
    dkCmdBufBarrier(cmd, DkBarrier_Fragments, DkInvalidateFlags_Image);
}

uint32_t GfxRenderingAPIDeko3d::PlayerShadowDepthSlot(const PicaTarget& target) {
    auto& slots = mPlayerShadow.DepthSlots;
    if (const auto found = slots.find(target.Id); found != slots.end()) return found->second;
    // Targets are made at a scene's start; a long list is mostly gone ones.
    if (slots.size() >= 8U) {
        for (const auto& [id, slot] : slots) mResources.FreeImageSlot(slot);
        slots.clear();
    }
    const uint32_t slot = mResources.AllocateImageSlot();
    if (slot >= PicaResources::kMaxImages) return slot;
    DkImageView view;
    dkImageViewDefaults(&view, &target.DepthStencil);
    view.dsSource = DkDsSource_Depth;
    mResources.WriteImageDescriptor(slot, view);
    slots.emplace(target.Id, slot);
    return slot;
}

void GfxRenderingAPIDeko3d::DrawPlayerShadow(PicaTarget& target) {
    auto& shadow = mPlayerShadow;
    if (shadow.Frame != mDevice.Frame() || shadow.Draws.empty()) return;
    // This target's draws, once; another target's wait for its own point.
    std::vector<PlayerShadowDraw> draws;
    std::erase_if(shadow.Draws, [&](const PlayerShadowDraw& draw) {
        if (draw.Target != &target) return false;
        draws.push_back(draw);
        return true;
    });
    if (draws.empty() || !shadow.Selected) return;
    const float opacity = std::clamp(shadow.Selected->Opacity, 0.0F, 1.0F);
    if (opacity <= 0.0F) return;
    const auto decal = shadow.Region && shadow.WorldToClip && shadow.Light && shadow.DecalFragment != nullptr &&
                               shadow.CoverVertex != nullptr && shadow.SilhouetteMemory != nullptr
                           ? Oot3d::PlayerShadowDecalOf(*shadow.Selected, *shadow.Region, *shadow.Light,
                                                        *shadow.WorldToClip)
                           : std::nullopt;
    const PlayerShadowDraw& first = draws.front();
    const bool minusOneFirst = Deko3d::ClipYSwizzleNegates(mFlipY);
    const std::array<float, 4> viewport{
        static_cast<float>(first.Viewport.X), static_cast<float>(first.Viewport.Y),
        static_cast<float>(first.Viewport.Width), static_cast<float>(first.Viewport.Height)};
    DkScissor area = first.Scissor;
    if (decal)
        if (const auto rect =
                Oot3d::PlayerShadowDecalScreenRect(*shadow.WorldToClip, *decal, *shadow.Region, viewport, minusOneFirst))
            area = Intersect(area, WithinTarget(*rect, target.Extent.Width, target.Extent.Height));
    const uint32_t depthSlot = decal ? PlayerShadowDepthSlot(target) : PicaResources::kMaxImages;
    if (!decal || depthSlot >= PicaResources::kMaxImages) {
        ++shadow.Unavailable;
        return;
    }
    if (area.width == 0U || area.height == 0U) return; // off screen

    PlayerShadowDecalUniforms block{};
    for (size_t row = 0; row < 4U; ++row)
        for (size_t column = 0; column < 4U; ++column)
            block.ClipToWorld[column * 4U + row] = decal->ClipToWorld[row * 4U + column];
    std::copy(viewport.begin(), viewport.end(), block.Viewport);
    const auto put = [](float* out, const std::array<float, 3>& xyz, float w) {
        out[0] = xyz[0];
        out[1] = xyz[1];
        out[2] = xyz[2];
        out[3] = w;
    };
    put(block.Origin, decal->Origin, minusOneFirst ? 1.0F : 0.0F);
    put(block.Normal, decal->Normal, decal->Rise);
    put(block.TowardLight, decal->TowardLight, Oot3d::kPlayerShadowDecalFloorFacing);
    put(block.Along, decal->Along, decal->AlongRange[0]);
    put(block.Across, decal->Across, decal->AcrossRange[0]);
    block.Extent[0] = 1.0F / (decal->AlongRange[1] - decal->AlongRange[0]);
    block.Extent[1] = 1.0F / (decal->AcrossRange[1] - decal->AcrossRange[0]);
    block.Extent[2] = decal->Lowest;
    block.Extent[3] = decal->Highest;
    put(block.Link, decal->Link, Oot3d::kPlayerShadowDecalLinkRadius);
    put(block.Middle, decal->Middle, Oot3d::kPlayerShadowDecalLinkTall);
    block.Shade[0] = opacity;
    block.Shade[1] = 1.0F / static_cast<float>(kPlayerShadowSilhouetteSize);
    block.Shade[2] = Oot3d::kPlayerShadowDecalFade;
    block.Shade[3] = Oot3d::kPlayerShadowDecalFloorRelief;
    // The scene's translucent floor (the Temple of Time's marble); none: no
    // point is under the lowest height.
    if (decal->Floor) {
        put(block.Eye, decal->Eye, decal->Floor->Height);
        block.FloorBox[0] = decal->Floor->X[0];
        block.FloorBox[1] = decal->Floor->X[1];
        block.FloorBox[2] = decal->Floor->Z[0];
        block.FloorBox[3] = decal->Floor->Z[1];
    } else {
        put(block.Eye, decal->Eye, std::numeric_limits<float>::lowest());
    }
    const uint32_t blockBytes = AlignUp(static_cast<uint32_t>(sizeof(block)), DK_UNIFORM_BUF_ALIGNMENT);
    const StreamSpan uniforms = mDevice.Stream(blockBytes, DK_UNIFORM_BUF_ALIGNMENT);
    if (uniforms.Cpu == nullptr) return;
    std::memcpy(uniforms.Cpu, &block, sizeof(block));

    DkCmdBuf cmd = mDevice.Cmd();
    mDevice.MarkShadowStart();
    DrawPlayerShadowSilhouette(draws);
    BindPicaTarget(target);
    ++target.Version;
    DkColorState colorState;
    dkColorStateDefaults(&colorState);
    dkColorStateSetBlendEnable(&colorState, 0, true);
    dkCmdBufBindColorState(cmd, &colorState);
    DkBlendState blend;
    dkBlendStateDefaults(&blend);
    blend.colorBlendOp = DkBlendOp_Add;
    blend.srcColorBlendFactor = DkBlendFactor_SrcAlpha;
    blend.dstColorBlendFactor = DkBlendFactor_InvSrcAlpha;
    blend.alphaBlendOp = DkBlendOp_Add;
    blend.srcAlphaBlendFactor = DkBlendFactor_Zero;
    blend.dstAlphaBlendFactor = DkBlendFactor_One;
    dkCmdBufBindBlendState(cmd, 0, &blend);
    DkColorWriteState rgb;
    dkColorWriteStateDefaults(&rgb);
    dkColorWriteStateSetMask(&rgb, 0, DkColorMask_RGB);
    dkCmdBufBindColorWriteState(cmd, &rgb);
    DkRasterizerState rasterizer;
    dkRasterizerStateDefaults(&rasterizer);
    rasterizer.depthClampEnable = 0;
    rasterizer.cullMode = DkFace_None;
    dkCmdBufBindRasterizerState(cmd, &rasterizer);
    DkDepthStencilState depthStencil;
    dkDepthStencilStateDefaults(&depthStencil);
    depthStencil.depthTestEnable = 0;
    depthStencil.depthWriteEnable = 0;
    depthStencil.stencilTestEnable = 0;
    dkCmdBufBindDepthStencilState(cmd, &depthStencil);
    // The depth is compressed: resolved, then sampled with only the colour bound.
    dkCmdBufResolveDepthValues(cmd);
    dkCmdBufBarrier(cmd, DkBarrier_Fragments, DkInvalidateFlags_Image | DkInvalidateFlags_Descriptors);
    const DkImageView* colors[] = {&target.ColorView};
    dkCmdBufBindRenderTargets(cmd, colors, 1, nullptr);
    mBoundTarget = nullptr; // the scene target, depth included, is bound again after
    const DkShader* shaders[] = {shadow.CoverVertex, shadow.DecalFragment};
    dkCmdBufBindShaders(cmd, DkStageFlag_Vertex | DkStageFlag_Fragment, shaders, 2);
    dkCmdBufBindUniformBuffer(cmd, DkStage_Fragment, 0, uniforms.Gpu, blockBytes);
    dkCmdBufBindTexture(cmd, DkStage_Fragment, 0, dkMakeTextureHandle(shadow.SilhouetteSlot, mLinearClampSampler));
    dkCmdBufBindTexture(cmd, DkStage_Fragment, 1, dkMakeTextureHandle(depthSlot, mLinearClampSampler));
    dkCmdBufBindVtxAttribState(cmd, nullptr, 0);
    dkCmdBufBindVtxBufferState(cmd, nullptr, 0);
    ApplyDkViewport(cmd, static_cast<float>(area.x), static_cast<float>(area.y), static_cast<float>(area.width),
                    static_cast<float>(area.height));
    dkCmdBufSetScissors(cmd, 0, &area, 1);
    dkCmdBufDraw(cmd, DkPrimitive_Triangles, 3, 1, 0, 0);
    // The next draws write the depth again.
    dkCmdBufBarrier(cmd, DkBarrier_Fragments, 0);
    mDevice.MarkShadowEnd(0U);
    mDevice.Checkpoint("shadow");
    shadow.Meshes += draws.size();
    shadow.Pixels += static_cast<uint64_t>(area.width) * area.height;
    if (shadow.Frames++ % kPlayerShadowReportFrames == 0U) {
        const auto bridgeCounters = Oot3d::PlayerShadowBridge::Instance().Counters();
        Log("shadow: frame %llu decal, %zu meshes opacity %.2f height %.0f | %llu frames, %llu meshes, %llu "
            "skipped, %llu without a decal, %llu pixels a frame | game frames %llu, with a shadow %llu, samples "
            "without their frame %llu",
            static_cast<unsigned long long>(mDevice.Frame()), draws.size(), static_cast<double>(opacity),
            static_cast<double>(shadow.Selected->Height), static_cast<unsigned long long>(shadow.Frames),
            static_cast<unsigned long long>(shadow.Meshes), static_cast<unsigned long long>(shadow.Skipped),
            static_cast<unsigned long long>(shadow.Unavailable),
            static_cast<unsigned long long>(shadow.Pixels / std::max<uint64_t>(shadow.Frames, 1U)),
            static_cast<unsigned long long>(bridgeCounters.Published),
            static_cast<unsigned long long>(bridgeCounters.WithShadow),
            static_cast<unsigned long long>(bridgeCounters.SamplesWithoutFrame));
    }
}

} // namespace Fast
#endif
