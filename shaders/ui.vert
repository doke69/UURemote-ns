#version 450
layout(location=0) out vec2 uv;
layout(std140,binding=0) uniform TextureRect {vec4 textureRect;};
void main() {
    vec2 p=vec2(float((gl_VertexID<<1)&2),float(gl_VertexID&2));
    uv=textureRect.xy+p*textureRect.zw;
    gl_Position=vec4(p*2.0-1.0,0.0,1.0);
}
