#version 460
// deko3d simple shadow (gfx_deko3d_player_shadow.cpp): one triangle over the
// whole viewport, which is the decal's rectangle; its fragment program is
// player_shadow_decal_fsh.glsl.
void main() {
    const vec2 positions[3] = vec2[3](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
    gl_Position = vec4(positions[gl_VertexID], 0.0, 1.0);
}
