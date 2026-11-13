#version 460
// deko3d simple shadow, method "decal" (gfx_deko3d_player_shadow.cpp): Link's
// silhouette cast on what the scene drew around him. Each pixel of the region's
// screen rectangle takes the scene's depth there back to its world point
// (PlayerShadowDecalWorldPoint), goes along the light into the silhouette and
// darkens by it, under the rules of PlayerShadowDecalLookup (player_shadow.h):
// only within the region's height range, not between Link and the light, not
// Link himself. Four bilinear taps soften the edge as in method "texture".
layout(std140, binding = 0) uniform PlayerShadowDecal {
    mat4 clip_to_world;  // world = clip_to_world * (ndc x, ndc y, -depth, 1), then / w
    vec4 viewport;       // x, y, width, height of Link's draws (target pixels)
    vec4 origin;         // xyz: the shadow plane's origin; w: 1 when NDC y = -1 is the viewport's first row
    vec4 normal;         // xyz: the floor's normal; w: toward_light . normal
    vec4 toward_light;   // xyz
    vec4 along;          // xyz; w: the region's first along
    vec4 across;         // xyz; w: the region's first across
    vec4 extent;         // x: 1 / along length, y: 1 / across length, z: lowest height, w: highest
    vec4 link;           // xyz: Link's position; w: his radius
    vec4 middle;         // xyz: his middle; w: his height
    vec4 shade;          // x: opacity, y: one silhouette texel, z: the fade's length
};
layout(binding = 0) uniform sampler2D silhouette;
layout(binding = 1) uniform sampler2D scene_depth;

layout(location = 0) out vec4 shadow_out_color;

void main() {
    float depth = texelFetch(scene_depth, ivec2(gl_FragCoord.xy), 0).r;
    if (depth >= 1.0) discard;
    float x = (gl_FragCoord.x - viewport.x) / viewport.z * 2.0 - 1.0;
    float row = (gl_FragCoord.y - viewport.y) / viewport.w * 2.0;
    float y = origin.w > 0.5 ? row - 1.0 : 1.0 - row;
    vec4 world = clip_to_world * vec4(x, y, -depth, 1.0);
    if (abs(world.w) <= 1.0e-6) discard;
    vec3 p = world.xyz / world.w;

    vec3 relative = p - origin.xyz;
    float height = dot(relative, normal.xyz);
    if (height < extent.z || height > extent.w) discard;
    if (dot(p - middle.xyz, toward_light.xyz) > 0.0) discard;
    vec3 from_link = p - link.xyz;
    float up = dot(from_link, normal.xyz);
    vec3 sideways = from_link - normal.xyz * up;
    if (dot(sideways, sideways) < link.w * link.w && up > 1.0 && up < middle.w) discard;

    vec3 flat_point = relative - toward_light.xyz * (height / normal.w);
    vec2 uv = vec2((dot(flat_point, along.xyz) - along.w) * extent.x,
                   (dot(flat_point, across.xyz) - across.w) * extent.y);
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) discard;
    // The silhouette's rows follow the scene's NDC-to-row rule, as the floor quad's texcoords.
    if (origin.w <= 0.5) uv.y = 1.0 - uv.y;

    float d = shade.y * 0.75;
    float cover = texture(silhouette, uv + vec2(-d, -d)).r + texture(silhouette, uv + vec2(d, -d)).r +
                  texture(silhouette, uv + vec2(-d, d)).r + texture(silhouette, uv + vec2(d, d)).r;
    if (cover <= 0.0) discard;
    float fade = clamp(min(height - extent.z, extent.w - height) / shade.z, 0.0, 1.0);
    shadow_out_color = vec4(0.0, 0.0, 0.0, shade.x * cover * 0.25 * fade);
}
