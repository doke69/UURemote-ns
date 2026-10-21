#version 450
layout(location=0) in vec2 uv;
layout(location=0) out vec4 color;
layout(binding=0) uniform sampler2D image;
layout(std140,binding=0) uniform Tint {vec4 tint;};
void main() {color=texture(image,uv)*tint;}
