#version 450

layout(location = 0) in vec2 vLocal;
layout(location = 1) in float vStatic;
layout(location = 2) in float vLightCount;

layout(location = 0) out vec4 outColor;

void main() {
    float dist = dot(vLocal, vLocal);
    if (dist > 1.0) {
        discard;
    }

    vec3 dynamicColor = vec3(0.92, 0.42, 0.27);
    vec3 staticColor = vec3(0.22, 0.68, 0.87);
    vec3 color = mix(dynamicColor, staticColor, clamp(vStatic, 0.0, 1.0));

    float rim = smoothstep(1.0, 0.2, dist);
    float glow = 0.08 * clamp(vLightCount, 0.0, 4.0);
    outColor = vec4(color * rim + glow, 1.0);
}
