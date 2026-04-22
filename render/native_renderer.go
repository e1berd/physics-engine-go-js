package render

/*
#cgo CFLAGS: -I/usr/include/SDL2 -I${SRCDIR}/../third_party
#cgo LDFLAGS: -lSDL2 -lvulkan
#include <stdlib.h>
#include "native_vulkan.h"
*/
import "C"

import (
	"encoding/binary"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"unsafe"
)

const maxNativeBodies = 7

type nativeRenderer struct {
	ptr *C.NativeRenderer
}

func newNativeRenderer(title string, width, height int) (*nativeRenderer, error) {
	vertPath, fragPath, err := compileShaders()
	if err != nil {
		return nil, err
	}

	vert, err := loadSPIRV(vertPath)
	if err != nil {
		return nil, err
	}
	frag, err := loadSPIRV(fragPath)
	if err != nil {
		return nil, err
	}

	titleC := C.CString(title)
	defer C.free(unsafe.Pointer(titleC))

	var errBuf [1024]C.char
	ptr := C.renderer_create(
		titleC,
		C.int(width),
		C.int(height),
		(*C.uint32_t)(unsafe.Pointer(&vert[0])),
		C.size_t(len(vert)),
		(*C.uint32_t)(unsafe.Pointer(&frag[0])),
		C.size_t(len(frag)),
		&errBuf[0],
		C.size_t(len(errBuf)),
	)
	if ptr == nil {
		return nil, fmt.Errorf(C.GoString(&errBuf[0]))
	}

	return &nativeRenderer{ptr: ptr}, nil
}

func (r *nativeRenderer) DeviceName() string {
	if r.ptr == nil {
		return ""
	}
	return C.GoString(C.renderer_device_name(r.ptr))
}

func (r *nativeRenderer) ShouldClose() bool {
	if r.ptr == nil {
		return true
	}
	return C.renderer_should_close(r.ptr) != 0
}

func (r *nativeRenderer) Render(scene SceneSnapshot) error {
	var packed [maxNativeBodies * 4]C.float
	count := len(scene.Bodies)
	if count > maxNativeBodies {
		count = maxNativeBodies
	}
	for i := 0; i < count; i++ {
		body := scene.Bodies[i]
		packed[i*4+0] = C.float(body.Position.X)
		packed[i*4+1] = C.float(body.Position.Y)
		packed[i*4+2] = C.float(body.Position.Z)

		radius := float32(body.Radius)
		if body.Shape == "box" {
			radius = -radius
		}
		packed[i*4+3] = C.float(radius)
	}

	var errBuf [1024]C.char
	res := C.renderer_render(
		r.ptr,
		&packed[0],
		C.int(count),
		C.float(scene.Time.ElapsedSeconds),
		C.int(len(scene.Lights)),
		C.float(scene.PlaneY),
		&errBuf[0],
		C.size_t(len(errBuf)),
	)
	if res == 0 {
		return fmt.Errorf(C.GoString(&errBuf[0]))
	}
	return nil
}

func (r *nativeRenderer) Close() {
	if r.ptr != nil {
		C.renderer_destroy(r.ptr)
		r.ptr = nil
	}
}

func compileShaders() (string, string, error) {
	tempDir, err := os.MkdirTemp("", "physics-engine-go-shaders-*")
	if err != nil {
		return "", "", fmt.Errorf("create shader temp dir: %w", err)
	}

	vertPath := filepath.Join(tempDir, "scene.vert.glsl")
	fragPath := filepath.Join(tempDir, "scene.frag.glsl")
	vertSPV := filepath.Join(tempDir, "scene.vert.spv")
	fragSPV := filepath.Join(tempDir, "scene.frag.spv")

	if err := os.WriteFile(vertPath, nativeVertexShader, 0o644); err != nil {
		return "", "", err
	}
	if err := os.WriteFile(fragPath, nativeFragmentShader, 0o644); err != nil {
		return "", "", err
	}

	if out, err := exec.Command("glslc", "-fshader-stage=vert", vertPath, "-o", vertSPV).CombinedOutput(); err != nil {
		return "", "", fmt.Errorf("compile vertex shader: %v: %s", err, string(out))
	}
	if out, err := exec.Command("glslc", "-fshader-stage=frag", fragPath, "-o", fragSPV).CombinedOutput(); err != nil {
		return "", "", fmt.Errorf("compile fragment shader: %v: %s", err, string(out))
	}
	return vertSPV, fragSPV, nil
}

func loadSPIRV(path string) ([]uint32, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return nil, err
	}
	if len(data)%4 != 0 {
		return nil, fmt.Errorf("invalid SPIR-V length for %s", path)
	}

	code := make([]uint32, len(data)/4)
	for i := 0; i < len(code); i++ {
		code[i] = binary.LittleEndian.Uint32(data[i*4 : i*4+4])
	}
	return code, nil
}

var nativeVertexShader = []byte(`#version 450
layout(push_constant) uniform PushData {
    vec4 bodies[7];
    vec4 meta;
} pc;

layout(location = 0) out vec2 vUV;

const vec2 kFullscreenTriangle[3] = vec2[](
    vec2(-1.0, -1.0),
    vec2( 3.0, -1.0),
    vec2(-1.0,  3.0)
);

void main() {
    vec2 pos = kFullscreenTriangle[gl_VertexIndex];
    gl_Position = vec4(pos, 0.0, 1.0);
    vUV = pos * 0.5 + 0.5;
}
`)

var nativeFragmentShader = []byte(`#version 450
layout(push_constant) uniform PushData {
    vec4 bodies[7];
    vec4 meta;
} pc;

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

const float MAX_DIST = 120.0;
const float EPS = 0.001;
const int MAX_STEPS = 96;

float sdSphere(vec3 p, float r) {
    return length(p) - r;
}

float sdBox(vec3 p, vec3 b) {
    vec3 q = abs(p) - b;
    return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0);
}

float mapScene(vec3 p, out int materialId) {
    float planeY = pc.meta.w;
    float best = p.y - planeY;
    materialId = 0;

    int count = int(pc.meta.x);
    for (int i = 0; i < count; ++i) {
        vec4 body = pc.bodies[i];
        float radius = abs(body.w);
        float d;
        if (body.w < 0.0) {
            d = sdBox(p - body.xyz, vec3(radius * 0.85));
        } else {
            d = sdSphere(p - body.xyz, radius);
        }
        if (d < best) {
            best = d;
            materialId = i + 1;
        }
    }
    return best;
}

vec3 calcNormal(vec3 p) {
    int m;
    vec2 e = vec2(0.001, 0.0);
    return normalize(vec3(
        mapScene(p + e.xyy, m) - mapScene(p - e.xyy, m),
        mapScene(p + e.yxy, m) - mapScene(p - e.yxy, m),
        mapScene(p + e.yyx, m) - mapScene(p - e.yyx, m)
    ));
}

vec3 materialColor(int materialId) {
    if (materialId == 0) {
        return vec3(0.72, 0.74, 0.78);
    }
    int idx = (materialId - 1) % 4;
    if (idx == 0) return vec3(0.92, 0.38, 0.26);
    if (idx == 1) return vec3(0.19, 0.63, 0.88);
    if (idx == 2) return vec3(0.94, 0.75, 0.24);
    return vec3(0.27, 0.78, 0.55);
}

float softShadow(vec3 ro, vec3 rd) {
    float res = 1.0;
    float t = 0.05;
    for (int i = 0; i < 24; ++i) {
        int m;
        float h = mapScene(ro + rd * t, m);
        if (h < 0.001) return 0.0;
        res = min(res, 12.0 * h / t);
        t += clamp(h, 0.02, 0.18);
        if (t > 18.0) break;
    }
    return clamp(res, 0.0, 1.0);
}

void main() {
    float aspect = max(pc.meta.y, 0.001);
    vec2 uv = vUV * 2.0 - 1.0;
    uv.x *= aspect;

    vec3 cameraPos = vec3(0.0, 5.6, 12.0);
    vec3 cameraTarget = vec3(0.0, 1.5, 0.0);
    vec3 forward = normalize(cameraTarget - cameraPos);
    vec3 right = normalize(cross(forward, vec3(0.0, 1.0, 0.0)));
    vec3 up = cross(right, forward);
    vec3 rayDir = normalize(forward + uv.x * right + uv.y * up);

    float t = 0.0;
    int materialId = -1;
    bool hit = false;
    for (int i = 0; i < MAX_STEPS; ++i) {
        vec3 p = cameraPos + rayDir * t;
        float d = mapScene(p, materialId);
        if (d < EPS) {
            hit = true;
            break;
        }
        t += d;
        if (t > MAX_DIST) {
            break;
        }
    }

    if (!hit) {
        vec3 sky = mix(vec3(0.78, 0.86, 0.96), vec3(0.18, 0.28, 0.45), clamp(0.5 + rayDir.y * 0.5, 0.0, 1.0));
        outColor = vec4(sky, 1.0);
        return;
    }

    vec3 hitPos = cameraPos + rayDir * t;
    vec3 normal = calcNormal(hitPos);
    vec3 lightDir = normalize(vec3(-0.5, 1.0, 0.35));
    float diffuse = max(dot(normal, lightDir), 0.0);
    float shadow = softShadow(hitPos + normal * 0.02, lightDir);
    vec3 viewDir = normalize(cameraPos - hitPos);
    vec3 halfDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(normal, halfDir), 0.0), 48.0);

    vec3 color = materialColor(materialId);
    if (materialId == 0) {
        float grid = step(0.96, abs(fract(hitPos.x * 0.5) - 0.5) * 2.0) + step(0.96, abs(fract(hitPos.z * 0.5) - 0.5) * 2.0);
        color *= 0.86 + min(grid, 1.0) * 0.18;
    }

    vec3 ambient = color * 0.22;
    vec3 lit = ambient + color * diffuse * shadow + vec3(1.0) * spec * shadow * 0.35;
    float fog = exp(-t * 0.045);
    vec3 sky = vec3(0.70, 0.80, 0.93);
    outColor = vec4(mix(sky, lit, fog), 1.0);
}
`)
