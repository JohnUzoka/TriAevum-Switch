#version 460
// deko3d simple shadow, method "decal" (gfx_deko3d_player_shadow.cpp): Link's
// silhouette cast on what the scene drew around him. Each pixel of the region's
// screen rectangle takes the scene's depth there back to its world point
// (PlayerShadowDecalWorldPoint), goes along the light into the silhouette and
// darkens by it, under the rules of PlayerShadowDecalLookup (player_shadow.h):
// only within the region's height range, not between Link and the light, not
// Link himself, unless it is the floor drawn a little above the ground under
// him. Four bilinear taps soften the edge, more away from his feet.
layout(std140, binding = 0) uniform PlayerShadowDecal {
    mat4 clip_to_world;  // world = clip_to_world * (ndc x, ndc y, -depth, 1), then / w
    vec4 viewport;       // x, y, width, height of Link's draws (target pixels)
    vec4 origin;         // xyz: the shadow plane's origin; w: 1 when NDC y = -1 is the viewport's first row
    vec4 normal;         // xyz: the floor's normal; w: toward_light . normal
    vec4 toward_light;   // xyz; w: the cosine a raised floor faces the normal within
    vec4 along;          // xyz; w: the region's first along
    vec4 across;         // xyz; w: the region's first across
    vec4 extent;         // x: 1 / along length, y: 1 / across length, z: lowest height, w: highest
    vec4 link;           // xyz: Link's position; w: his radius
    vec4 middle;         // xyz: his middle; w: his height
    vec4 shade;          // x: opacity, y: one silhouette texel, z: the fade's length, w: the raised floor's height
};
layout(binding = 0) uniform sampler2D silhouette;
layout(binding = 1) uniform sampler2D scene_depth;

layout(location = 0) out vec4 shadow_out_color;

// PlayerShadowDecalRaisedFloor with PlayerShadowDecalFacing: within Link's
// cylinder, a receiver up to shade.w above his position whose surface (from
// the next pixels' world points, their depths along the scene depth's
// derivatives) faces along the floor normal is the floor drawn above the
// ground under him, not his body. Only pixels in the cylinder pay for it.
bool raised_floor(vec3 p, float up, vec2 ndc, float depth, float depth_dx, float depth_dy) {
    if (up <= 1.0 || up >= shade.w) return false;
    vec2 step = vec2(2.0 / viewport.z, (origin.w > 0.5 ? 2.0 : -2.0) / viewport.w);
    vec4 next_across = clip_to_world * vec4(ndc.x + step.x, ndc.y, -(depth + depth_dx), 1.0);
    vec4 next_down = clip_to_world * vec4(ndc.x, ndc.y + step.y, -(depth + depth_dy), 1.0);
    vec3 facing = cross(next_across.xyz / next_across.w - p, next_down.xyz / next_down.w - p);
    float facing_along = dot(facing, normal.xyz);
    float facing_length = dot(facing, facing);
    return facing_length > 0.0 && facing_along * facing_along >= toward_light.w * toward_light.w * facing_length;
}

void main() {
    float depth = texelFetch(scene_depth, ivec2(gl_FragCoord.xy), 0).r;
    // Before any discard: derivatives need the whole 2x2 quad.
    float depth_dx = dFdx(depth);
    float depth_dy = dFdy(depth);
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
    if (dot(sideways, sideways) < link.w * link.w &&
        !raised_floor(p, up, vec2(x, y), depth, depth_dx, depth_dy) && up > 1.0 && up < middle.w) discard;

    vec3 flat_point = relative - toward_light.xyz * (height / normal.w);
    vec2 uv = vec2((dot(flat_point, along.xyz) - along.w) * extent.x,
                   (dot(flat_point, across.xyz) - across.w) * extent.y);
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) discard;
    // The silhouette's rows follow the scene's NDC-to-row rule, as the floor quad's texcoords.
    if (origin.w <= 0.5) uv.y = 1.0 - uv.y;

    // Crisp where it meets his feet, softer farther along (as a real shadow):
    // the same four taps, spread from 0.75 to 2.25 texels over 60 units of the
    // plane; wider, four taps show a doubled edge.
    float d = shade.y * mix(0.75, 2.25, clamp(length(flat_point) / 60.0, 0.0, 1.0));
    float cover = texture(silhouette, uv + vec2(-d, -d)).r + texture(silhouette, uv + vec2(d, -d)).r +
                  texture(silhouette, uv + vec2(-d, d)).r + texture(silhouette, uv + vec2(d, d)).r;
    if (cover <= 0.0) discard;
    float fade = clamp(min(height - extent.z, extent.w - height) / shade.z, 0.0, 1.0);
    shadow_out_color = vec4(0.0, 0.0, 0.0, shade.x * cover * 0.25 * fade);
}
