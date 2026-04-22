#include "native_vulkan.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vulkan/vulkan.h"

#define MAX_BODIES 7

typedef struct PushData {
    float bodies[MAX_BODIES][4];
    float meta[4];
} PushData;

struct NativeRenderer {
    SDL_Window* window;
    VkInstance instance;
    VkSurfaceKHR surface;
    VkPhysicalDevice physical_device;
    VkDevice device;
    VkQueue graphics_queue;
    VkQueue present_queue;
    uint32_t graphics_queue_family;
    uint32_t present_queue_family;
    VkSwapchainKHR swapchain;
    VkFormat swapchain_format;
    VkExtent2D swapchain_extent;
    uint32_t swapchain_image_count;
    VkImage* swapchain_images;
    VkImageView* image_views;
    VkFramebuffer* framebuffers;
    VkRenderPass render_pass;
    VkPipelineLayout pipeline_layout;
    VkPipeline pipeline;
    VkCommandPool command_pool;
    VkCommandBuffer* command_buffers;
    VkSemaphore image_available;
    VkSemaphore render_finished;
    VkFence in_flight_fence;
    char device_name[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE];
};

static void set_error(char* err, size_t err_cap, const char* message) {
    if (!err || err_cap == 0) {
        return;
    }
    snprintf(err, err_cap, "%s", message);
}

static void set_error_vk(char* err, size_t err_cap, const char* where, VkResult result) {
    if (!err || err_cap == 0) {
        return;
    }
    snprintf(err, err_cap, "%s: VkResult=%d", where, (int)result);
}

static bool create_instance(NativeRenderer* r, const char* title, char* err, size_t err_cap) {
    unsigned ext_count = 0;
    if (!SDL_Vulkan_GetInstanceExtensions(r->window, &ext_count, NULL)) {
        set_error(err, err_cap, SDL_GetError());
        return false;
    }

    const char** ext_names = calloc(ext_count, sizeof(const char*));
    if (!ext_names) {
        set_error(err, err_cap, "calloc ext_names failed");
        return false;
    }
    if (!SDL_Vulkan_GetInstanceExtensions(r->window, &ext_count, ext_names)) {
        free(ext_names);
        set_error(err, err_cap, SDL_GetError());
        return false;
    }

    VkApplicationInfo app_info = {0};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = title;
    app_info.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    app_info.pEngineName = "PhysicsEngineGo";
    app_info.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    app_info.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo create_info = {0};
    create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create_info.pApplicationInfo = &app_info;
    create_info.enabledExtensionCount = ext_count;
    create_info.ppEnabledExtensionNames = ext_names;

    VkResult result = vkCreateInstance(&create_info, NULL, &r->instance);
    free(ext_names);
    if (result != VK_SUCCESS) {
        set_error_vk(err, err_cap, "vkCreateInstance", result);
        return false;
    }

    if (!SDL_Vulkan_CreateSurface(r->window, r->instance, &r->surface)) {
        set_error(err, err_cap, SDL_GetError());
        return false;
    }
    return true;
}

static bool find_queue_families(NativeRenderer* r, VkPhysicalDevice device, uint32_t* graphics_index, uint32_t* present_index) {
    uint32_t queue_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_count, NULL);
    if (queue_count == 0) {
        return false;
    }

    VkQueueFamilyProperties* families = calloc(queue_count, sizeof(VkQueueFamilyProperties));
    if (!families) {
        return false;
    }
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_count, families);

    bool graphics_found = false;
    bool present_found = false;
    for (uint32_t i = 0; i < queue_count; ++i) {
        if (families[i].queueCount > 0 && (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
            *graphics_index = i;
            graphics_found = true;
        }
        VkBool32 supported = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, r->surface, &supported);
        if (families[i].queueCount > 0 && supported) {
            *present_index = i;
            present_found = true;
        }
    }

    free(families);
    return graphics_found && present_found;
}

static bool has_swapchain_extension(VkPhysicalDevice device) {
    uint32_t count = 0;
    vkEnumerateDeviceExtensionProperties(device, NULL, &count, NULL);
    if (count == 0) {
        return false;
    }
    VkExtensionProperties* props = calloc(count, sizeof(VkExtensionProperties));
    if (!props) {
        return false;
    }
    vkEnumerateDeviceExtensionProperties(device, NULL, &count, props);
    bool found = false;
    for (uint32_t i = 0; i < count; ++i) {
        if (strcmp(props[i].extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0) {
            found = true;
            break;
        }
    }
    free(props);
    return found;
}

static bool pick_physical_device(NativeRenderer* r, char* err, size_t err_cap) {
    uint32_t device_count = 0;
    VkResult result = vkEnumeratePhysicalDevices(r->instance, &device_count, NULL);
    if (result != VK_SUCCESS || device_count == 0) {
        set_error_vk(err, err_cap, "vkEnumeratePhysicalDevices", result);
        return false;
    }

    VkPhysicalDevice* devices = calloc(device_count, sizeof(VkPhysicalDevice));
    if (!devices) {
        set_error(err, err_cap, "calloc devices failed");
        return false;
    }
    vkEnumeratePhysicalDevices(r->instance, &device_count, devices);

    for (uint32_t i = 0; i < device_count; ++i) {
        uint32_t graphics_index = 0;
        uint32_t present_index = 0;
        if (!has_swapchain_extension(devices[i])) {
            continue;
        }
        if (find_queue_families(r, devices[i], &graphics_index, &present_index)) {
            r->physical_device = devices[i];
            r->graphics_queue_family = graphics_index;
            r->present_queue_family = present_index;
            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(devices[i], &props);
            snprintf(r->device_name, sizeof(r->device_name), "%s", props.deviceName);
            free(devices);
            return true;
        }
    }

    free(devices);
    set_error(err, err_cap, "no suitable Vulkan physical device found");
    return false;
}

static bool create_device(NativeRenderer* r, char* err, size_t err_cap) {
    float queue_priority = 1.0f;
    VkDeviceQueueCreateInfo queue_infos[2];
    memset(queue_infos, 0, sizeof(queue_infos));

    uint32_t queue_info_count = 1;
    queue_infos[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_infos[0].queueFamilyIndex = r->graphics_queue_family;
    queue_infos[0].queueCount = 1;
    queue_infos[0].pQueuePriorities = &queue_priority;

    if (r->present_queue_family != r->graphics_queue_family) {
        queue_infos[1] = queue_infos[0];
        queue_infos[1].queueFamilyIndex = r->present_queue_family;
        queue_info_count = 2;
    }

    const char* extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo create_info = {0};
    create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    create_info.queueCreateInfoCount = queue_info_count;
    create_info.pQueueCreateInfos = queue_infos;
    create_info.enabledExtensionCount = 1;
    create_info.ppEnabledExtensionNames = extensions;

    VkResult result = vkCreateDevice(r->physical_device, &create_info, NULL, &r->device);
    if (result != VK_SUCCESS) {
        set_error_vk(err, err_cap, "vkCreateDevice", result);
        return false;
    }

    vkGetDeviceQueue(r->device, r->graphics_queue_family, 0, &r->graphics_queue);
    vkGetDeviceQueue(r->device, r->present_queue_family, 0, &r->present_queue);
    return true;
}

static VkSurfaceFormatKHR choose_surface_format(const VkSurfaceFormatKHR* formats, uint32_t count) {
    for (uint32_t i = 0; i < count; ++i) {
        if (formats[i].format == VK_FORMAT_B8G8R8A8_SRGB && formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return formats[i];
        }
    }
    for (uint32_t i = 0; i < count; ++i) {
        if (formats[i].format == VK_FORMAT_B8G8R8A8_UNORM) {
            return formats[i];
        }
    }
    return formats[0];
}

static VkPresentModeKHR choose_present_mode(const VkPresentModeKHR* modes, uint32_t count) {
    for (uint32_t i = 0; i < count; ++i) {
        if (modes[i] == VK_PRESENT_MODE_MAILBOX_KHR) {
            return modes[i];
        }
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

static bool create_swapchain(NativeRenderer* r, int width, int height, char* err, size_t err_cap) {
    VkSurfaceCapabilitiesKHR caps;
    VkResult result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(r->physical_device, r->surface, &caps);
    if (result != VK_SUCCESS) {
        set_error_vk(err, err_cap, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR", result);
        return false;
    }

    uint32_t format_count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(r->physical_device, r->surface, &format_count, NULL);
    if (format_count == 0) {
        set_error(err, err_cap, "no surface formats");
        return false;
    }
    VkSurfaceFormatKHR* formats = calloc(format_count, sizeof(VkSurfaceFormatKHR));
    vkGetPhysicalDeviceSurfaceFormatsKHR(r->physical_device, r->surface, &format_count, formats);

    uint32_t present_count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(r->physical_device, r->surface, &present_count, NULL);
    if (present_count == 0) {
        free(formats);
        set_error(err, err_cap, "no present modes");
        return false;
    }
    VkPresentModeKHR* present_modes = calloc(present_count, sizeof(VkPresentModeKHR));
    vkGetPhysicalDeviceSurfacePresentModesKHR(r->physical_device, r->surface, &present_count, present_modes);

    VkSurfaceFormatKHR surface_format = choose_surface_format(formats, format_count);
    VkPresentModeKHR present_mode = choose_present_mode(present_modes, present_count);
    free(formats);
    free(present_modes);

    VkExtent2D extent;
    if (caps.currentExtent.width != UINT32_MAX) {
        extent = caps.currentExtent;
    } else {
        extent.width = (uint32_t)width;
        extent.height = (uint32_t)height;
        if (extent.width < caps.minImageExtent.width) extent.width = caps.minImageExtent.width;
        if (extent.width > caps.maxImageExtent.width) extent.width = caps.maxImageExtent.width;
        if (extent.height < caps.minImageExtent.height) extent.height = caps.minImageExtent.height;
        if (extent.height > caps.maxImageExtent.height) extent.height = caps.maxImageExtent.height;
    }

    uint32_t image_count = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && image_count > caps.maxImageCount) {
        image_count = caps.maxImageCount;
    }

    uint32_t queue_family_indices[2] = {r->graphics_queue_family, r->present_queue_family};
    VkSwapchainCreateInfoKHR create_info = {0};
    create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    create_info.surface = r->surface;
    create_info.minImageCount = image_count;
    create_info.imageFormat = surface_format.format;
    create_info.imageColorSpace = surface_format.colorSpace;
    create_info.imageExtent = extent;
    create_info.imageArrayLayers = 1;
    create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (r->graphics_queue_family != r->present_queue_family) {
        create_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        create_info.queueFamilyIndexCount = 2;
        create_info.pQueueFamilyIndices = queue_family_indices;
    } else {
        create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }
    create_info.preTransform = caps.currentTransform;
    create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    create_info.presentMode = present_mode;
    create_info.clipped = VK_TRUE;

    result = vkCreateSwapchainKHR(r->device, &create_info, NULL, &r->swapchain);
    if (result != VK_SUCCESS) {
        set_error_vk(err, err_cap, "vkCreateSwapchainKHR", result);
        return false;
    }

    r->swapchain_format = surface_format.format;
    r->swapchain_extent = extent;

    vkGetSwapchainImagesKHR(r->device, r->swapchain, &r->swapchain_image_count, NULL);
    r->swapchain_images = calloc(r->swapchain_image_count, sizeof(VkImage));
    r->image_views = calloc(r->swapchain_image_count, sizeof(VkImageView));
    r->framebuffers = calloc(r->swapchain_image_count, sizeof(VkFramebuffer));
    r->command_buffers = calloc(r->swapchain_image_count, sizeof(VkCommandBuffer));
    if (!r->swapchain_images || !r->image_views || !r->framebuffers || !r->command_buffers) {
        set_error(err, err_cap, "calloc swapchain arrays failed");
        return false;
    }
    vkGetSwapchainImagesKHR(r->device, r->swapchain, &r->swapchain_image_count, r->swapchain_images);
    return true;
}

static bool create_image_views(NativeRenderer* r, char* err, size_t err_cap) {
    for (uint32_t i = 0; i < r->swapchain_image_count; ++i) {
        VkImageViewCreateInfo create_info = {0};
        create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        create_info.image = r->swapchain_images[i];
        create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        create_info.format = r->swapchain_format;
        create_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        create_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        create_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        create_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        create_info.subresourceRange.levelCount = 1;
        create_info.subresourceRange.layerCount = 1;
        VkResult result = vkCreateImageView(r->device, &create_info, NULL, &r->image_views[i]);
        if (result != VK_SUCCESS) {
            set_error_vk(err, err_cap, "vkCreateImageView", result);
            return false;
        }
    }
    return true;
}

static bool create_render_pass(NativeRenderer* r, char* err, size_t err_cap) {
    VkAttachmentDescription color_attachment = {0};
    color_attachment.format = r->swapchain_format;
    color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference color_ref = {0};
    color_ref.attachment = 0;
    color_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass = {0};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_ref;

    VkSubpassDependency dependency = {0};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo create_info = {0};
    create_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    create_info.attachmentCount = 1;
    create_info.pAttachments = &color_attachment;
    create_info.subpassCount = 1;
    create_info.pSubpasses = &subpass;
    create_info.dependencyCount = 1;
    create_info.pDependencies = &dependency;

    VkResult result = vkCreateRenderPass(r->device, &create_info, NULL, &r->render_pass);
    if (result != VK_SUCCESS) {
        set_error_vk(err, err_cap, "vkCreateRenderPass", result);
        return false;
    }
    return true;
}

static bool create_shader_module(NativeRenderer* r, const uint32_t* code, size_t word_count, VkShaderModule* module, char* err, size_t err_cap) {
    VkShaderModuleCreateInfo create_info = {0};
    create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    create_info.codeSize = word_count * sizeof(uint32_t);
    create_info.pCode = code;
    VkResult result = vkCreateShaderModule(r->device, &create_info, NULL, module);
    if (result != VK_SUCCESS) {
        set_error_vk(err, err_cap, "vkCreateShaderModule", result);
        return false;
    }
    return true;
}

static bool create_pipeline(NativeRenderer* r, const uint32_t* vert_words, size_t vert_word_count, const uint32_t* frag_words, size_t frag_word_count, char* err, size_t err_cap) {
    VkShaderModule vert_module;
    VkShaderModule frag_module;
    if (!create_shader_module(r, vert_words, vert_word_count, &vert_module, err, err_cap)) {
        return false;
    }
    if (!create_shader_module(r, frag_words, frag_word_count, &frag_module, err, err_cap)) {
        vkDestroyShaderModule(r->device, vert_module, NULL);
        return false;
    }

    VkPipelineShaderStageCreateInfo stages[2];
    memset(stages, 0, sizeof(stages));
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert_module;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag_module;
    stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vertex_input = {0};
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo input_assembly = {0};
    input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport viewport = {0};
    viewport.width = (float)r->swapchain_extent.width;
    viewport.y = (float)r->swapchain_extent.height;
    viewport.height = -(float)r->swapchain_extent.height;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor = {0};
    scissor.extent = r->swapchain_extent;

    VkPipelineViewportStateCreateInfo viewport_state = {0};
    viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.pViewports = &viewport;
    viewport_state.scissorCount = 1;
    viewport_state.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer = {0};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisampling = {0};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState color_blend_attachment = {0};
    color_blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo color_blending = {0};
    color_blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blending.attachmentCount = 1;
    color_blending.pAttachments = &color_blend_attachment;

    VkDynamicState dynamics[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic_state = {0};
    dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state.dynamicStateCount = 2;
    dynamic_state.pDynamicStates = dynamics;

    VkPushConstantRange push_range = {0};
    push_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    push_range.offset = 0;
    push_range.size = sizeof(PushData);

    VkPipelineLayoutCreateInfo layout_info = {0};
    layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_info.pushConstantRangeCount = 1;
    layout_info.pPushConstantRanges = &push_range;

    VkResult result = vkCreatePipelineLayout(r->device, &layout_info, NULL, &r->pipeline_layout);
    if (result != VK_SUCCESS) {
        set_error_vk(err, err_cap, "vkCreatePipelineLayout", result);
        vkDestroyShaderModule(r->device, frag_module, NULL);
        vkDestroyShaderModule(r->device, vert_module, NULL);
        return false;
    }

    VkGraphicsPipelineCreateInfo pipeline_info = {0};
    pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.stageCount = 2;
    pipeline_info.pStages = stages;
    pipeline_info.pVertexInputState = &vertex_input;
    pipeline_info.pInputAssemblyState = &input_assembly;
    pipeline_info.pViewportState = &viewport_state;
    pipeline_info.pRasterizationState = &rasterizer;
    pipeline_info.pMultisampleState = &multisampling;
    pipeline_info.pColorBlendState = &color_blending;
    pipeline_info.pDynamicState = &dynamic_state;
    pipeline_info.layout = r->pipeline_layout;
    pipeline_info.renderPass = r->render_pass;
    pipeline_info.subpass = 0;

    result = vkCreateGraphicsPipelines(r->device, VK_NULL_HANDLE, 1, &pipeline_info, NULL, &r->pipeline);
    vkDestroyShaderModule(r->device, frag_module, NULL);
    vkDestroyShaderModule(r->device, vert_module, NULL);
    if (result != VK_SUCCESS) {
        set_error_vk(err, err_cap, "vkCreateGraphicsPipelines", result);
        return false;
    }
    return true;
}

static bool create_framebuffers(NativeRenderer* r, char* err, size_t err_cap) {
    for (uint32_t i = 0; i < r->swapchain_image_count; ++i) {
        VkImageView attachments[] = {r->image_views[i]};
        VkFramebufferCreateInfo create_info = {0};
        create_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        create_info.renderPass = r->render_pass;
        create_info.attachmentCount = 1;
        create_info.pAttachments = attachments;
        create_info.width = r->swapchain_extent.width;
        create_info.height = r->swapchain_extent.height;
        create_info.layers = 1;
        VkResult result = vkCreateFramebuffer(r->device, &create_info, NULL, &r->framebuffers[i]);
        if (result != VK_SUCCESS) {
            set_error_vk(err, err_cap, "vkCreateFramebuffer", result);
            return false;
        }
    }
    return true;
}

static bool create_command_pool_and_buffers(NativeRenderer* r, char* err, size_t err_cap) {
    VkCommandPoolCreateInfo pool_info = {0};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = r->graphics_queue_family;
    VkResult result = vkCreateCommandPool(r->device, &pool_info, NULL, &r->command_pool);
    if (result != VK_SUCCESS) {
        set_error_vk(err, err_cap, "vkCreateCommandPool", result);
        return false;
    }

    VkCommandBufferAllocateInfo alloc_info = {0};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool = r->command_pool;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = r->swapchain_image_count;
    result = vkAllocateCommandBuffers(r->device, &alloc_info, r->command_buffers);
    if (result != VK_SUCCESS) {
        set_error_vk(err, err_cap, "vkAllocateCommandBuffers", result);
        return false;
    }
    return true;
}

static bool create_sync_objects(NativeRenderer* r, char* err, size_t err_cap) {
    VkSemaphoreCreateInfo sem_info = {0};
    sem_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fence_info = {0};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    VkResult result = vkCreateSemaphore(r->device, &sem_info, NULL, &r->image_available);
    if (result != VK_SUCCESS) {
        set_error_vk(err, err_cap, "vkCreateSemaphore image_available", result);
        return false;
    }
    result = vkCreateSemaphore(r->device, &sem_info, NULL, &r->render_finished);
    if (result != VK_SUCCESS) {
        set_error_vk(err, err_cap, "vkCreateSemaphore render_finished", result);
        return false;
    }
    result = vkCreateFence(r->device, &fence_info, NULL, &r->in_flight_fence);
    if (result != VK_SUCCESS) {
        set_error_vk(err, err_cap, "vkCreateFence", result);
        return false;
    }
    return true;
}

static void destroy_renderer_contents(NativeRenderer* r) {
    if (!r) return;
    if (r->device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(r->device);
    }
    if (r->in_flight_fence != VK_NULL_HANDLE) vkDestroyFence(r->device, r->in_flight_fence, NULL);
    if (r->render_finished != VK_NULL_HANDLE) vkDestroySemaphore(r->device, r->render_finished, NULL);
    if (r->image_available != VK_NULL_HANDLE) vkDestroySemaphore(r->device, r->image_available, NULL);
    if (r->command_pool != VK_NULL_HANDLE) vkDestroyCommandPool(r->device, r->command_pool, NULL);
    if (r->framebuffers) {
        for (uint32_t i = 0; i < r->swapchain_image_count; ++i) {
            if (r->framebuffers[i] != VK_NULL_HANDLE) vkDestroyFramebuffer(r->device, r->framebuffers[i], NULL);
        }
        free(r->framebuffers);
    }
    if (r->pipeline != VK_NULL_HANDLE) vkDestroyPipeline(r->device, r->pipeline, NULL);
    if (r->pipeline_layout != VK_NULL_HANDLE) vkDestroyPipelineLayout(r->device, r->pipeline_layout, NULL);
    if (r->render_pass != VK_NULL_HANDLE) vkDestroyRenderPass(r->device, r->render_pass, NULL);
    if (r->image_views) {
        for (uint32_t i = 0; i < r->swapchain_image_count; ++i) {
            if (r->image_views[i] != VK_NULL_HANDLE) vkDestroyImageView(r->device, r->image_views[i], NULL);
        }
        free(r->image_views);
    }
    if (r->swapchain != VK_NULL_HANDLE) vkDestroySwapchainKHR(r->device, r->swapchain, NULL);
    if (r->device != VK_NULL_HANDLE) vkDestroyDevice(r->device, NULL);
    if (r->surface != VK_NULL_HANDLE) vkDestroySurfaceKHR(r->instance, r->surface, NULL);
    if (r->instance != VK_NULL_HANDLE) vkDestroyInstance(r->instance, NULL);
    if (r->window) SDL_DestroyWindow(r->window);
    free(r->swapchain_images);
    free(r->command_buffers);
    SDL_Quit();
}

NativeRenderer* renderer_create(const char* title, int width, int height, const uint32_t* vert_words, size_t vert_word_count, const uint32_t* frag_words, size_t frag_word_count, char* err, size_t err_cap) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        set_error(err, err_cap, SDL_GetError());
        return NULL;
    }

    NativeRenderer* r = calloc(1, sizeof(NativeRenderer));
    if (!r) {
        SDL_Quit();
        set_error(err, err_cap, "calloc renderer failed");
        return NULL;
    }

    r->window = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height, SDL_WINDOW_VULKAN | SDL_WINDOW_SHOWN);
    if (!r->window) {
        set_error(err, err_cap, SDL_GetError());
        destroy_renderer_contents(r);
        free(r);
        return NULL;
    }

    if (!create_instance(r, title, err, err_cap) ||
        !pick_physical_device(r, err, err_cap) ||
        !create_device(r, err, err_cap) ||
        !create_swapchain(r, width, height, err, err_cap) ||
        !create_image_views(r, err, err_cap) ||
        !create_render_pass(r, err, err_cap) ||
        !create_pipeline(r, vert_words, vert_word_count, frag_words, frag_word_count, err, err_cap) ||
        !create_framebuffers(r, err, err_cap) ||
        !create_command_pool_and_buffers(r, err, err_cap) ||
        !create_sync_objects(r, err, err_cap)) {
        destroy_renderer_contents(r);
        free(r);
        return NULL;
    }

    return r;
}

void renderer_destroy(NativeRenderer* r) {
    if (!r) return;
    destroy_renderer_contents(r);
    free(r);
}

int renderer_should_close(NativeRenderer* r) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
            return 1;
        }
    }
    return 0;
}

const char* renderer_device_name(NativeRenderer* r) {
    if (!r) return "";
    return r->device_name;
}

static bool record_command_buffer(NativeRenderer* r, uint32_t image_index, const PushData* push, int body_count, int light_count, float elapsed, char* err, size_t err_cap) {
    VkCommandBuffer cmd = r->command_buffers[image_index];
    VkCommandBufferBeginInfo begin_info = {0};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    VkResult result = vkBeginCommandBuffer(cmd, &begin_info);
    if (result != VK_SUCCESS) {
        set_error_vk(err, err_cap, "vkBeginCommandBuffer", result);
        return false;
    }

    VkClearValue clear = {0};
    clear.color.float32[0] = 0.06f;
    clear.color.float32[1] = 0.08f + (light_count > 4 ? 4 : light_count) * 0.03f;
    clear.color.float32[2] = 0.12f + elapsed * 0.02f;
    clear.color.float32[3] = 1.0f;

    VkRenderPassBeginInfo pass_info = {0};
    pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    pass_info.renderPass = r->render_pass;
    pass_info.framebuffer = r->framebuffers[image_index];
    pass_info.renderArea.extent = r->swapchain_extent;
    pass_info.clearValueCount = 1;
    pass_info.pClearValues = &clear;

    vkCmdBeginRenderPass(cmd, &pass_info, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipeline);

    VkViewport viewport = {0};
    viewport.width = (float)r->swapchain_extent.width;
    viewport.height = (float)r->swapchain_extent.height;
    viewport.maxDepth = 1.0f;
    VkRect2D scissor = {0};
    scissor.extent = r->swapchain_extent;
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdPushConstants(cmd, r->pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushData), push);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(cmd);

    result = vkEndCommandBuffer(cmd);
    if (result != VK_SUCCESS) {
        set_error_vk(err, err_cap, "vkEndCommandBuffer", result);
        return false;
    }
    return true;
}

int renderer_render(NativeRenderer* r, const float* bodies_xyri, int body_count, float elapsed_seconds, int light_count, float plane_y, char* err, size_t err_cap) {
    if (!r) {
        set_error(err, err_cap, "renderer is nil");
        return 0;
    }

    VkResult result = vkWaitForFences(r->device, 1, &r->in_flight_fence, VK_TRUE, UINT64_MAX);
    if (result != VK_SUCCESS) {
        set_error_vk(err, err_cap, "vkWaitForFences", result);
        return 0;
    }

    uint32_t image_index = 0;
    result = vkAcquireNextImageKHR(r->device, r->swapchain, UINT64_MAX, r->image_available, VK_NULL_HANDLE, &image_index);
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        set_error_vk(err, err_cap, "vkAcquireNextImageKHR", result);
        return 0;
    }

    vkResetFences(r->device, 1, &r->in_flight_fence);
    vkResetCommandBuffer(r->command_buffers[image_index], 0);

    PushData push;
    memset(&push, 0, sizeof(push));
    if (body_count > MAX_BODIES) body_count = MAX_BODIES;
    for (int i = 0; i < body_count; ++i) {
        push.bodies[i][0] = bodies_xyri[i * 4 + 0];
        push.bodies[i][1] = bodies_xyri[i * 4 + 1];
        push.bodies[i][2] = bodies_xyri[i * 4 + 2];
        push.bodies[i][3] = bodies_xyri[i * 4 + 3];
    }
    push.meta[0] = (float)body_count;
    push.meta[1] = r->swapchain_extent.height > 0 ? (float)r->swapchain_extent.width / (float)r->swapchain_extent.height : 1.0f;
    push.meta[2] = elapsed_seconds;
    push.meta[3] = plane_y;

    if (!record_command_buffer(r, image_index, &push, body_count, light_count, elapsed_seconds, err, err_cap)) {
        return 0;
    }

    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit_info = {0};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.waitSemaphoreCount = 1;
    submit_info.pWaitSemaphores = &r->image_available;
    submit_info.pWaitDstStageMask = &wait_stage;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &r->command_buffers[image_index];
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = &r->render_finished;

    result = vkQueueSubmit(r->graphics_queue, 1, &submit_info, r->in_flight_fence);
    if (result != VK_SUCCESS) {
        set_error_vk(err, err_cap, "vkQueueSubmit", result);
        return 0;
    }

    VkPresentInfoKHR present_info = {0};
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = &r->render_finished;
    present_info.swapchainCount = 1;
    present_info.pSwapchains = &r->swapchain;
    present_info.pImageIndices = &image_index;

    result = vkQueuePresentKHR(r->present_queue, &present_info);
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        set_error_vk(err, err_cap, "vkQueuePresentKHR", result);
        return 0;
    }
    return 1;
}
