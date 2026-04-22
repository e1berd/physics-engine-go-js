#ifndef PHYSICS_ENGINE_GO_NATIVE_VULKAN_H
#define PHYSICS_ENGINE_GO_NATIVE_VULKAN_H

#include <stddef.h>
#include <stdint.h>

typedef struct NativeRenderer NativeRenderer;

NativeRenderer* renderer_create(
    const char* title,
    int width,
    int height,
    const uint32_t* vert_words,
    size_t vert_word_count,
    const uint32_t* frag_words,
    size_t frag_word_count,
    char* err,
    size_t err_cap
);

void renderer_destroy(NativeRenderer* renderer);
int renderer_should_close(NativeRenderer* renderer);
const char* renderer_device_name(NativeRenderer* renderer);
int renderer_render(
    NativeRenderer* renderer,
    const float* bodies_xyri,
    int body_count,
    float elapsed_seconds,
    int light_count,
    float plane_y,
    char* err,
    size_t err_cap
);

#endif
