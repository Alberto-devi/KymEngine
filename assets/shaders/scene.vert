#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;
layout(location = 3) in vec2 inUV;

layout(set = 0, binding = 0) uniform SceneUniforms {
    mat4 viewProj;
    vec4 dirLightDirIntensity;
    vec4 dirLightColorSky;
    vec4 skyZenith;
    vec4 skyHorizon;
    vec4 skyGround;
    vec4 cameraPosAndPointCount;
    vec4 pointLightPosRange[8];
    vec4 pointLightColorInt[8];
} ubo;

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 normalCol0; // xyz = col0, w = metallic
    vec4 normalCol1; // xyz = col1
    vec4 normalCol2; // xyz = col2
    vec4 baseColor;  // rgb = albedo/tint + emissive, a = packed mode/roughness
} pc;

layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec3 fragColor;
layout(location = 2) out vec2 fragUV;
layout(location = 3) out float fragMode;
layout(location = 4) out float fragMetallic;
layout(location = 5) out vec3 fragWorldPos;

void main() {
    float mode = pc.baseColor.a;
    vec4 worldPos = pc.model * vec4(inPosition, 1.0);
    fragWorldPos = worldPos.xyz;
    fragColor = inColor * pc.baseColor.rgb;
    fragUV = inUV;
    fragMode = mode;
    fragMetallic = clamp(pc.normalCol0.w, 0.0, 1.0);

    if (mode <= -2.5 && mode > -3.5) {
        // Skybox sphere/dome drawn at infinity depth (z = w)
        vec4 clipPos = ubo.viewProj * worldPos;
        gl_Position = clipPos.xyww;
        fragNormal = normalize(inPosition);
    } else {
        gl_Position = ubo.viewProj * worldPos;
        mat3 normalMatrix = mat3(pc.normalCol0.xyz, pc.normalCol1.xyz, pc.normalCol2.xyz);
        fragNormal = normalize(normalMatrix * inNormal);
    }
}
