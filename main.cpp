#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>

#include <cassert>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "config.hpp"
#include "mesh.hpp"
#include "shader.hpp"

enum Texture
{
    TextureDiffuseX1,
    TextureDiffuseX2,
    TextureFinal,
    TextureCount,
};

static constexpr int Width = 960;
static constexpr int Height = 720;
static constexpr float Zoom = 1.0f;
static constexpr float Pan = 0.0002f;
static constexpr float Fov = glm::radians(60.0f);
static constexpr float Near = 0.1f;
static constexpr float Far = 1000.0f;
static constexpr glm::vec3 Up{0.0f, 1.0f, 0.0f};

static SDL_Window* window;
static SDL_GPUDevice* device;
static SDL_GPUGraphicsPipeline* linePipeline;
static SDL_GPUGraphicsPipeline* voxelPipeline;
static SDL_GPUComputePipeline* clearPipeline;
static SDL_GPUComputePipeline* diffusePipeline;
static SDL_GPUTexture* colorTexture;
static SDL_GPUTexture* depthTexture;
static SDL_GPUTexture* textures[TextureCount];
static int size = 64;
static int dt;
static uint64_t time1;
static uint64_t time2;
static float diffusion;
static float viscosity;
static uint32_t width;
static uint32_t height;
static glm::vec3 position;
static float pitch;
static float yaw;
static float distance = 100.0f;
static glm::mat4 viewProj;

static bool Init()
{
    SDL_SetAppMetadata("Fluid Simulation", nullptr, nullptr);
    SDL_SetLogPriorities(SDL_LOG_PRIORITY_VERBOSE);
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        SDL_Log("Failed to initialize SDL: %s", SDL_GetError());
        return false;
    }
    window = SDL_CreateWindow("Fluid Simulation", 960, 720, SDL_WINDOW_RESIZABLE);
    if (!window)
    {
        SDL_Log("Failed to create window: %s", SDL_GetError());
        return false;
    }
#if defined(SDL_PLATFORM_WIN32)
    device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_DXIL, true, nullptr);
#elif defined(SDL_PLATFORM_APPLE)
    device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_MSL, true, nullptr);
#else
    device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, true, nullptr);
#endif
    if (!device)
    {
        SDL_Log("Failed to create device: %s", SDL_GetError());
        return false;
    }
    if (!SDL_ClaimWindowForGPUDevice(device, window))
    {
        SDL_Log("Failed to create swapchain: %s", SDL_GetError());
        return false;
    }
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui_ImplSDL3_InitForSDLGPU(window);
    ImGui_ImplSDLGPU3_InitInfo info{};
    info.Device = device;
    info.ColorTargetFormat = SDL_GetGPUSwapchainTextureFormat(device, window);
    ImGui_ImplSDLGPU3_Init(&info);
    return true;
}

static bool CreatePipelines()
{
    SDL_GPUShader* voxelFragShader = LoadShader(device, "voxel.frag");
    SDL_GPUShader* voxelVertShader = LoadShader(device, "voxel.vert");
    SDL_GPUShader* lineFragShader = LoadShader(device, "line.frag");
    SDL_GPUShader* lineVertShader = LoadShader(device, "line.vert");
    if (!voxelFragShader || !voxelVertShader || !lineFragShader || !lineVertShader)
    {
        SDL_Log("Failed to create shader(s)");
        return false;
    }
    SDL_GPUVertexBufferDescription buffer{};
    SDL_GPUVertexAttribute attrib{};
    SDL_GPUColorTargetDescription target{};
    buffer.slot = 0;
    buffer.pitch = sizeof(float) * 3;
    buffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    buffer.instance_step_rate = 0;
    attrib.location = 0;
    attrib.buffer_slot = 0;
    attrib.format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    attrib.offset = 0;
    target.format = SDL_GetGPUSwapchainTextureFormat(device, window);
    SDL_GPUGraphicsPipelineCreateInfo info{};
    info.vertex_shader = voxelVertShader;
    info.fragment_shader = voxelFragShader;
    info.vertex_input_state.vertex_buffer_descriptions = &buffer;
    info.vertex_input_state.num_vertex_buffers = 1;
    info.vertex_input_state.vertex_attributes = &attrib;
    info.vertex_input_state.num_vertex_attributes = 1;
    info.target_info.color_target_descriptions = &target;
    info.target_info.num_color_targets = 1;
    info.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    info.target_info.has_depth_stencil_target = true;
    info.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;
    info.depth_stencil_state.enable_depth_test = true;
    info.depth_stencil_state.enable_depth_write = true;
    voxelPipeline = SDL_CreateGPUGraphicsPipeline(device, &info);
    info.vertex_shader = lineVertShader;
    info.fragment_shader = lineFragShader;
    info.primitive_type = SDL_GPU_PRIMITIVETYPE_LINELIST;
    linePipeline = SDL_CreateGPUGraphicsPipeline(device, &info);
    diffusePipeline = LoadComputePipeline(device, "diffuse.comp");
    clearPipeline = LoadComputePipeline(device, "clear.comp");
    if (!voxelPipeline || !linePipeline || !clearPipeline || !diffusePipeline)
    {
        SDL_Log("Failed to create voxel pipeline: %s", SDL_GetError());
        return false;
    }
    SDL_ReleaseGPUShader(device, voxelFragShader);
    SDL_ReleaseGPUShader(device, voxelVertShader);
    SDL_ReleaseGPUShader(device, lineFragShader);
    SDL_ReleaseGPUShader(device, lineVertShader);
    return true;
}

static bool CreateTextures()
{
    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.width = Width;
    info.height = Height;
    info.layer_count_or_depth = 1;
    info.num_levels = 1;
    info.format = SDL_GetGPUSwapchainTextureFormat(device, window);
    info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    colorTexture = SDL_CreateGPUTexture(device, &info);
    if (!colorTexture)
    {
        SDL_Log("Failed to create texture: %s", SDL_GetError());
        return false;
    }
    info.format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    info.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    info.props = SDL_CreateProperties();
    SDL_SetFloatProperty(info.props, SDL_PROP_GPU_TEXTURE_CREATE_D3D12_CLEAR_DEPTH_FLOAT, 1.0f);
    depthTexture = SDL_CreateGPUTexture(device, &info);
    if (!depthTexture)
    {
        SDL_Log("Failed to create texture: %s", SDL_GetError());
        return false;
    }
    SDL_DestroyProperties(info.props);
    return true;
}

static void ClearTexture(SDL_GPUTexture* texture, SDL_GPUCommandBuffer* commandBuffer)
{
    SDL_GPUStorageTextureReadWriteBinding binding{};
    binding.texture = texture;
    SDL_GPUComputePass* computePass = SDL_BeginGPUComputePass(commandBuffer, &binding, 1, nullptr, 0);
    if (!computePass)
    {
        SDL_Log("Failed to begin compute pass: %s", SDL_GetError());
        return;
    }
    struct
    {
        uint32_t size;
    }
    params;
    params.size = size;
    int groups = (size + THREADS_3D - 1) / THREADS_3D;
    SDL_BindGPUComputePipeline(computePass, clearPipeline);
    SDL_PushGPUComputeUniformData(commandBuffer, 0, &params, sizeof(params));
    SDL_DispatchGPUCompute(computePass, groups, groups, groups);
    SDL_EndGPUComputePass(computePass);
}

static bool CreateCells()
{
    for (int i = 0; i < TextureCount; i++)
    {
        SDL_ReleaseGPUTexture(device, textures[i]);
        textures[i] = nullptr;
    }
    SDL_GPUCommandBuffer* commandBuffer = SDL_AcquireGPUCommandBuffer(device);
    if (!commandBuffer)
    {
        SDL_Log("Failed to acquire command buffer: %s", SDL_GetError());
        return false;
    }
    SDL_GPUTextureCreateInfo info{};
    info.format = SDL_GPU_TEXTUREFORMAT_R32_FLOAT;
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.usage = SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_READ |
        SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE |
        SDL_GPU_TEXTUREUSAGE_GRAPHICS_STORAGE_READ;
    info.width = size;
    info.height = size;
    info.layer_count_or_depth = size;
    info.num_levels = 1;
    for (int i = 0; i < TextureCount; i++)
    {
        textures[i] = SDL_CreateGPUTexture(device, &info);
        if (!textures[i])
        {
            SDL_Log("Failed to create texture: %d, %s", i, SDL_GetError());
            return false;
        }
        ClearTexture(textures[i], commandBuffer);
    }
    SDL_SubmitGPUCommandBuffer(commandBuffer);
    return true;
}

static void Swap(Texture texture1, Texture texture2)
{
    SDL_GPUTexture* texture = textures[texture1];
    textures[texture1] = textures[texture2];
    textures[texture2] = texture;
}

static void UpdateViewProj()
{
    glm::vec3 vector;
    vector.x = std::cos(pitch) * std::cos(yaw);
    vector.y = std::sin(pitch);
    vector.z = std::cos(pitch) * std::sin(yaw);
    float ratio = static_cast<float>(Width) / Height;
    glm::vec3 center = glm::vec3(size / 2);
    glm::vec3 position = center - vector * distance;
    glm::mat4 view = glm::lookAt(position, position + vector, Up);
    glm::mat4 proj = glm::perspective(Fov, ratio, Near, Far);
    viewProj = proj * view;
}

static void RenderCube(SDL_GPUCommandBuffer* commandBuffer)
{
    SDL_GPUColorTargetInfo colorInfo{};
    SDL_GPUDepthStencilTargetInfo depthInfo{};
    colorInfo.texture = colorTexture;
    colorInfo.load_op = SDL_GPU_LOADOP_CLEAR;
    colorInfo.store_op = SDL_GPU_STOREOP_STORE;
    colorInfo.cycle = true;
    depthInfo.texture = depthTexture;
    depthInfo.load_op = SDL_GPU_LOADOP_CLEAR;
    depthInfo.stencil_load_op = SDL_GPU_LOADOP_CLEAR;
    depthInfo.store_op = SDL_GPU_STOREOP_STORE;
    depthInfo.clear_depth = 1.0f;
    depthInfo.cycle = true;
    SDL_GPURenderPass* renderPass = SDL_BeginGPURenderPass(commandBuffer, &colorInfo, 1, &depthInfo);
    if (!renderPass)
    {
        SDL_Log("Failed to begin render pass: %s", SDL_GetError());
        return;
    }
    struct
    {
        uint32_t size;
    }
    params;
    params.size = size;
    SDL_BindGPUGraphicsPipeline(renderPass, linePipeline);
    SDL_PushGPUVertexUniformData(commandBuffer, 0, &viewProj, sizeof(viewProj));
    SDL_PushGPUVertexUniformData(commandBuffer, 1, &params, sizeof(params));
    RenderMesh(renderPass, MeshTypeLineCube, 1);
    SDL_EndGPURenderPass(renderPass);
}

static void RenderVoxel(SDL_GPUCommandBuffer* commandBuffer)
{
    SDL_GPUColorTargetInfo colorInfo{};
    SDL_GPUDepthStencilTargetInfo depthInfo{};
    colorInfo.texture = colorTexture;
    colorInfo.load_op = SDL_GPU_LOADOP_LOAD;
    colorInfo.store_op = SDL_GPU_STOREOP_STORE;
    depthInfo.texture = depthTexture;
    depthInfo.load_op = SDL_GPU_LOADOP_LOAD;
    depthInfo.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass* renderPass = SDL_BeginGPURenderPass(commandBuffer, &colorInfo, 1, &depthInfo);
    if (!renderPass)
    {
        SDL_Log("Failed to begin render pass: %s", SDL_GetError());
        return;
    }
    SDL_BindGPUGraphicsPipeline(renderPass, voxelPipeline);
    SDL_BindGPUVertexStorageTextures(renderPass, 0, &textures[TextureFinal], 1);
    SDL_PushGPUVertexUniformData(commandBuffer, 0, &viewProj, sizeof(viewProj));
    RenderMesh(renderPass, MeshTypeTriangleCube, size * size * size);
    SDL_EndGPURenderPass(renderPass);
}

static void RenderRaymarch(SDL_GPUCommandBuffer* commandBuffer)
{
    /* TODO: */
}

static void RenderImGui(SDL_GPUCommandBuffer* commandBuffer, SDL_GPUTexture* swapchainTexture)
{
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize.x = width;
    io.DisplaySize.y = height;
    ImGui_ImplSDLGPU3_NewFrame();
    ImGui::NewFrame();
    /* TODO: */
    ImGui::Render();
    ImGui_ImplSDLGPU3_PrepareDrawData(ImGui::GetDrawData(), commandBuffer);
    SDL_GPUColorTargetInfo info{};
    info.texture = swapchainTexture;
    info.load_op = SDL_GPU_LOADOP_LOAD;
    info.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass* renderPass = SDL_BeginGPURenderPass(commandBuffer, &info, 1, nullptr);
    if (!renderPass)
    {
        SDL_Log("Failed to begin render pass: %s", SDL_GetError());
        return;
    }
    ImGui_ImplSDLGPU3_RenderDrawData(ImGui::GetDrawData(), commandBuffer, renderPass);
    SDL_EndGPURenderPass(renderPass);
}

static void Diffuse(SDL_GPUCommandBuffer* commandBuffer, Texture readTexture, Texture writeTexture)
{
    ClearTexture(textures[writeTexture], commandBuffer);
    for (uint32_t offset = 0; offset < 27; offset++)
    {
        SDL_GPUStorageTextureReadWriteBinding binding{};
        binding.texture = textures[writeTexture];
        SDL_GPUComputePass* computePass = SDL_BeginGPUComputePass(commandBuffer, &binding, 1, nullptr, 0);
        if (!computePass)
        {
            SDL_Log("Failed to begin compute pass: %s", SDL_GetError());
            SDL_CancelGPUCommandBuffer(commandBuffer);
            return;
        }
        int groups = (size / 3 + THREADS_3D - 1) / THREADS_3D;
        struct
        {
            uint32_t size;
            uint32_t offset;
            uint32_t dt;
        }
        params;
        params.size = size;
        params.offset = offset;
        params.dt = dt;
        SDL_BindGPUComputePipeline(computePass, diffusePipeline);
        SDL_BindGPUComputeStorageTextures(computePass, 0, &textures[readTexture], 1);
        SDL_PushGPUComputeUniformData(commandBuffer, 0, &params, sizeof(params));
        SDL_DispatchGPUCompute(computePass, groups, groups, groups);
        SDL_EndGPUComputePass(computePass);
    }
    Swap(readTexture, writeTexture);
}

static void Letterbox(SDL_GPUCommandBuffer* commandBuffer, SDL_GPUTexture* swapchainTexture)
{
    SDL_GPUBlitInfo info{};
    const float colorRatio = static_cast<float>(Width) / Height;
    const float swapchainRatio = static_cast<float>(width) / height;
    float scale = 0.0f;
    float letterboxW = 0.0f;
    float letterboxH = 0.0f;
    float letterboxX = 0.0f;
    float letterboxY = 0.0f;
    if (colorRatio > swapchainRatio)
    {
        scale = static_cast<float>(width) / Width;
        letterboxW = width;
        letterboxH = Height * scale;
        letterboxX = 0.0f;
        letterboxY = (height - letterboxH) / 2.0f;
    }
    else
    {
        scale = static_cast<float>(height) / Height;
        letterboxH = height;
        letterboxW = Width * scale;
        letterboxX = (width - letterboxW) / 2.0f;
        letterboxY = 0.0f;
    }
    SDL_FColor clearColor = {0.02f, 0.02f, 0.02f, 1.0f};
    info.load_op = SDL_GPU_LOADOP_CLEAR;
    info.clear_color = clearColor;
    info.source.texture = colorTexture;
    info.source.w = Width;
    info.source.h = Height;
    info.destination.texture = swapchainTexture;
    info.destination.x = letterboxX;
    info.destination.y = letterboxY;
    info.destination.w = letterboxW;
    info.destination.h = letterboxH;
    info.filter = SDL_GPU_FILTER_NEAREST;
    SDL_BlitGPUTexture(commandBuffer, &info);
}

static void Render()
{
    SDL_GPUCommandBuffer* commandBuffer = SDL_AcquireGPUCommandBuffer(device);
    if (!commandBuffer)
    {
        SDL_Log("Failed to acquire command buffer: %s", SDL_GetError());
        return;
    }
    SDL_GPUTexture* swapchainTexture;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(commandBuffer, window, &swapchainTexture, &width, &height))
    {
        SDL_Log("Failed to acquire swapchain texture: %s", SDL_GetError());
        SDL_CancelGPUCommandBuffer(commandBuffer);
        return;
    }
    if (!swapchainTexture || !width || !height)
    {
        /* NOTE: not an error */
        SDL_CancelGPUCommandBuffer(commandBuffer);
        return;
    }
    UpdateViewProj();
    Diffuse(commandBuffer, TextureDiffuseX1, TextureDiffuseX2);
    RenderCube(commandBuffer);
    RenderVoxel(commandBuffer);
    Letterbox(commandBuffer, swapchainTexture);
    RenderImGui(commandBuffer, swapchainTexture);
    SDL_SubmitGPUCommandBuffer(commandBuffer);
}

int main(int argc, char** argv)
{
    if (!Init())
    {
        SDL_Log("Failed to initialize");
        return 1;
    }
    if (!CreatePipelines())
    {
        SDL_Log("Failed to create pipelines");
        return 1;
    }
    if (!CreateMeshes(device))
    {
        SDL_Log("Failed to create meshes");
        return 1;
    }
    if (!CreateTextures())
    {
        SDL_Log("Failed to create textures");
        return 1;
    }
    if (!CreateCells())
    {
        SDL_Log("Failed to create cells");
        return 1;
    }
    bool running = true;
    while (running)
    {
        time2 = SDL_GetTicks();
        dt = time2 - time1;
        time1 = time2;
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL3_ProcessEvent(&event);
            switch (event.type)
            {
            case SDL_EVENT_MOUSE_WHEEL:
                distance = std::max(1.0f, distance - event.wheel.y * Zoom * dt);
                break;
            case SDL_EVENT_MOUSE_MOTION:
                if (event.motion.state & SDL_BUTTON_RMASK)
                {
                    float limit = glm::pi<float>() / 2.0f - 0.01f;
                    yaw += event.motion.xrel * Pan * dt;
                    pitch -= event.motion.yrel * Pan * dt;
                    pitch = std::clamp(pitch, -limit, limit);
                }
                break;
            case SDL_EVENT_QUIT:
                running = false;
                break;
            }
        }
        if (!running)
        {
            break;
        }
        Render();
    }
    SDL_HideWindow(window);
    for (int i = 0; i < TextureCount; i++)
    {
        SDL_ReleaseGPUTexture(device, textures[i]);
    }
    FreeMeshes(device);
    SDL_ReleaseGPUTexture(device, colorTexture);
    SDL_ReleaseGPUTexture(device, depthTexture);
    SDL_ReleaseGPUComputePipeline(device, clearPipeline);
    SDL_ReleaseGPUComputePipeline(device, diffusePipeline);
    SDL_ReleaseGPUGraphicsPipeline(device, voxelPipeline);
    SDL_ReleaseGPUGraphicsPipeline(device, linePipeline);
    ImGui_ImplSDLGPU3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_ReleaseWindowFromGPUDevice(device, window);
    SDL_DestroyGPUDevice(device);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}