#version 450
layout(location=0) in vec2 uv;
layout(location=0) out vec4 color;
layout(binding=0) uniform sampler2D luma;
layout(binding=1) uniform sampler2D chroma;
layout(std140,binding=0) uniform ColorMatrix {vec4 row0;vec4 row1;vec4 row2;};
void main() {
    vec4 sampleYuv=vec4(texture(luma,uv).r,texture(chroma,uv).rg,1.0);
    color=vec4(clamp(vec3(dot(row0,sampleYuv),dot(row1,sampleYuv),dot(row2,sampleYuv)),0.0,1.0),1.0);
}
