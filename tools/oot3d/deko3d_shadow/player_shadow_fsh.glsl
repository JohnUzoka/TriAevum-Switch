#version 460
// deko3d simple shadow, the silhouette (gfx_deko3d_player_shadow.cpp): Link's
// opaque meshes, flattened along the light by their own PICA vertex program
// with patched view and projection rows, cover their texels of the floor
// region in a constant colour (white, read through its red channel).
layout(std140, binding = 0) uniform PlayerShadow {
    vec4 shadow_color; // rgb black, a the opacity
};

layout(location = 0) out vec4 shadow_out_color;

void main() {
    shadow_out_color = shadow_color;
}
