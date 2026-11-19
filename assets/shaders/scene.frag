#version 450

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

layout(set = 1, binding = 0) uniform sampler2D uAlbedoTex;

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec3 fragColor;
layout(location = 2) in vec2 fragUV;
layout(location = 3) in float fragMode;
layout(location = 4) in float fragMetallic;
layout(location = 5) in vec3 fragWorldPos;

layout(location = 0) out vec4 outColor;

void main() {
    // 1. Planar Ground Shadow pass
    if (fragMode <= -3.5) {
        outColor = vec4(0.05, 0.07, 0.11, 0.26);
        return;
    }

    // 2. Procedural Skybox pass
    if (fragMode <= -2.5) {
        vec3 dir = normalize(fragNormal);
        float upFactor = smoothstep(0.0, 0.75, max(dir.y, 0.0));
        float downFactor = smoothstep(0.0, 0.45, max(-dir.y, 0.0));
        vec3 skyCol = mix(ubo.skyHorizon.rgb, ubo.skyZenith.rgb, upFactor);
        skyCol = mix(skyCol, ubo.skyGround.rgb, downFactor);
        skyCol = clamp(skyCol * 1.12 + vec3(0.05, 0.06, 0.08), 0.0, 1.0);
        outColor = vec4(skyCol, 1.0);
        return;
    }

    vec4 texSample = texture(uAlbedoTex, fragUV);

    // 3. 2D Sprite pass (supports RGBA PNG sprites & animated spritesheets)
    if (fragMode < 0.0) {
        if (texSample.a < 0.05) {
            discard;
        }
        vec3 color = fragColor * texSample.rgb;
        if (fragMode < -1.5) {
            float checker = mod(floor(fragUV.x * 6.0) + floor(fragUV.y * 6.0), 2.0);
            color *= mix(0.68, 1.0, checker);
        }
        outColor = vec4(color, texSample.a);
        return;
    }

    // 4. 3D Mesh PBR pass (Directional + Point Lights + Albedo Texture)
    if (texSample.a < 0.05) {
        discard;
    }

    bool isPlane = (fragMode >= 1.5);
    float roughness = clamp(isPlane ? (fragMode - 2.0) : fragMode, 0.05, 1.0);
    float metallic = clamp(fragMetallic, 0.0, 1.0);

    vec3 albedo = fragColor * texSample.rgb;
    if (isPlane) {
        vec2 grid = abs(fract(fragUV * 16.0 - 0.5) - 0.5) / fwidth(fragUV * 16.0);
        float line = min(grid.x, grid.y);
        float gridFactor = 1.0 - min(line, 1.0);
        albedo = mix(albedo, albedo * 1.55 + vec3(0.06), gridFactor * 0.45);
    }

    vec3 N = normalize(fragNormal);
    vec3 L = normalize(ubo.dirLightDirIntensity.xyz);
    vec3 V = normalize(ubo.cameraPosAndPointCount.xyz - fragWorldPos);
    vec3 H = normalize(L + V);

    float ndotl = max(dot(N, L), 0.0);
    float wrapDiff = clamp((dot(N, L) + 0.35) / 1.35, 0.0, 1.0);
    float fillDiff = max(dot(N, -L), 0.0) * 0.22;
    float ndoth = max(dot(N, H), 0.0);
    float ndotv = max(dot(N, V), 0.0);

    vec3 skyAmbient = mix(ubo.skyGround.rgb * 0.85 + vec3(0.28, 0.30, 0.35),
                          ubo.skyHorizon.rgb * 0.75 + vec3(0.32, 0.36, 0.44),
                          N.y * 0.5 + 0.5);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    vec3 fresnel = F0 + (vec3(1.0) - F0) * pow(1.0 - ndotv, 5.0);

    vec3 diffuseAlbedo = albedo * (1.0 - metallic * 0.72);
    float shininess = mix(112.0, 6.0, roughness);
    float specFactor = pow(ndoth, shininess) * mix(1.05, 0.15, roughness) * step(0.001, ndotl);
    vec3 dirRadiance = ubo.dirLightColorSky.rgb * ubo.dirLightDirIntensity.w;
    vec3 specular = (fresnel * specFactor) * dirRadiance + fresnel * skyAmbient * (0.28 * metallic * (1.0 - roughness));

    vec3 lit = diffuseAlbedo * (skyAmbient + dirRadiance * (ndotl * 0.85 + wrapDiff * 0.25 + fillDiff)) + specular;

    // Accumulate Point Lights
    int pointCount = int(ubo.cameraPosAndPointCount.w + 0.5);
    for (int i = 0; i < pointCount && i < 8; ++i) {
        vec3 toLight = ubo.pointLightPosRange[i].xyz - fragWorldPos;
        float dist = length(toLight);
        float range = max(ubo.pointLightPosRange[i].w, 0.1);
        if (dist < range) {
            vec3 pL = toLight / max(dist, 0.0001);
            vec3 pH = normalize(pL + V);
            float atten = clamp(1.0 - dist / range, 0.0, 1.0);
            atten *= atten;
            float pNdotL = max(dot(N, pL), 0.0);
            float pSpec = pow(max(dot(N, pH), 0.0), shininess) * mix(0.95, 0.12, roughness) * step(0.001, pNdotL);
            vec3 pRad = ubo.pointLightColorInt[i].rgb * ubo.pointLightColorInt[i].a * atten;
            lit += (diffuseAlbedo * pNdotL + fresnel * pSpec) * pRad;
        }
    }

    outColor = vec4(lit, 1.0);
}
