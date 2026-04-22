#version 450

layout(push_constant) uniform PushData {
    vec4 bodies[7];
    vec4 meta;
} pc;

layout(location = 0) out vec2 vLocal;
layout(location = 1) out float vStatic;
layout(location = 2) out float vLightCount;

const vec2 kCorners[6] = vec2[](
    vec2(-1.0, -1.0),
    vec2( 1.0, -1.0),
    vec2( 1.0,  1.0),
    vec2(-1.0, -1.0),
    vec2( 1.0,  1.0),
    vec2(-1.0,  1.0)
);

void main() {
    int bodyIndex = gl_VertexIndex / 6;
    int bodyCount = int(pc.meta.x);
    if (bodyIndex >= bodyCount) {
        gl_Position = vec4(2.0, 2.0, 0.0, 1.0);
        vLocal = vec2(2.0);
        vStatic = 0.0;
        vLightCount = pc.meta.w;
        return;
    }

    vec4 body = pc.bodies[bodyIndex];
    vec2 local = kCorners[gl_VertexIndex % 6];
    float aspect = max(pc.meta.y, 0.001);

    vec2 worldScale = vec2(0.10 / aspect, 0.10);
    vec2 center = vec2(body.x, body.y - 4.0) * worldScale;
    vec2 radius = vec2(body.z) * worldScale;

    gl_Position = vec4(center + local * radius, 0.0, 1.0);
    vLocal = local;
    vStatic = body.w;
    vLightCount = pc.meta.w;
}
