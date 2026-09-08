#version 450
layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 FragColor;
layout(set = 0, binding = 0) uniform sampler2D texture1;
layout(set = 1, binding = 0) uniform sampler2D maskTexture;
layout(push_constant) uniform PushConstants {
    vec2 viewport;     // 0: 蒙版 UV 归一化用
    float enableMask;  // 8
    float enableColor; // 12
    float opa;         // 16
    float pad;              // 20；std140 将下一 vec4 对齐到 32
    vec4 uniformColor;      // 32
    vec4 colorModulation;   // 48：采样后乘性染色，identity=(1,1,1,1)
} pc;
void main()
{
    vec4 maskColor = vec4(1.0);
    if (pc.enableMask > 0.5) {
        vec2 normalizedCoord = gl_FragCoord.xy / pc.viewport;
        maskColor = texture(maskTexture, normalizedCoord);
    }
    vec4 color = texture(texture1, texCoord);
    if (pc.enableMask > 0.5 && maskColor.a < 0.5) {
        discard;
    } else {
        if (pc.enableColor > 0.5) {
            color = vec4(pc.uniformColor.xyz, pc.uniformColor.a * color.a);
        }
        color *= pc.colorModulation;
        color.a = color.a * pc.opa;
        FragColor = color;
    }
}
