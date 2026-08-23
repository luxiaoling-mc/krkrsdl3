#version 450
layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 FragColor;
layout(set = 0, binding = 0) uniform sampler2D texture1;
layout(push_constant) uniform PushConstants {
    float opa;         // 0
    int method;        // 4（LayerBlendMethod）
    vec4 uniformColor; // 16（std140 16 字节对齐）
} pc;

// Layer 合成（软件 RenderManager bm* 语义）：
//   0 COPY / 8 COPYCOLOR / 10 COPYMASK：颜色原样输出，混合状态由管线决定
//   1 ALPHA / 2 CONSTALPHA：alpha 通道按软件 (x * opa8) >> 8 折算（×255/256）
//   3 ADD：RGB 按 opa 缩放，alpha 置 0（不影响目标 alpha）
//   4 SUB / 5 MUL / 6 MUL_HDA：RGB = 1-(1-src)*opa（软件 255-(255-src)*opa8>>8）
//   7 FILL：输出 uniformColor
//   9 COPYOPAQUE：alpha 置 1
void main()
{
    vec4 c = texture(texture1, texCoord);
    if (pc.method == 7) {
        c = pc.uniformColor;
    } else if (pc.method == 2) {
        c.a = floor(pc.opa * 255.0 * (255.0 / 256.0)) / 255.0;
    } else if (pc.method == 1) {
        c.a = floor(c.a * pc.opa * 255.0 * (255.0 / 256.0)) / 255.0;
    } else if (pc.method == 3) {
        c.rgb = c.rgb * pc.opa * (255.0 / 256.0);
        c.a = 0.0;
    } else if (pc.method == 4 || pc.method == 5 || pc.method == 6) {
        c.rgb = 1.0 - (1.0 - c.rgb) * pc.opa * (255.0 / 256.0);
    } else if (pc.method == 9) {
        c.a = 1.0;
    }
    FragColor = c;
}
