#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>

#include <cstdint>

#include "shader.hpp"
#include "upload_buffer.hpp"

enum Texture
{
    TextureS,
    TextureDensity,
    TextureVx,
    TextureVy,
    TextureVz,
    TextureVx0,
    TextureVy0,
    TextureVz0,
    TextureCount,
};

static constexpr int Width = 960;
static constexpr int Height = 720;

static SDL_Window* window;
static SDL_GPUDevice* device;
static SDL_GPUGraphicsPipeline* voxelPipeline;
static SDL_GPUTexture* colorTexture;
static SDL_GPUTexture* depthTexture;
static SDL_GPUTexture* textures[TextureCount];
static int size = 64;
static float dt;
static float diffusion;
static float viscosity;
static uint32_t width;
static uint32_t height;

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
    if (!voxelFragShader || !voxelVertShader)
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
    if (!voxelPipeline)
    {
        SDL_Log("Failed to create voxel pipeline: %s", SDL_GetError());
        return false;
    }
    SDL_ReleaseGPUShader(device, voxelFragShader);
    SDL_ReleaseGPUShader(device, voxelVertShader);
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

static bool CreateCells()
{
    for (int i = 0; i < TextureCount; i++)
    {
        SDL_ReleaseGPUTexture(device, textures[i]);
        textures[i] = nullptr;
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
    }
    return true;
}

static void RenderVoxel(SDL_GPUCommandBuffer* commandBuffer)
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
    SDL_BindGPUGraphicsPipeline(renderPass, voxelPipeline);
    SDL_EndGPURenderPass(renderPass);
}

static void RenderRaymarch(SDL_GPUCommandBuffer* commandBuffer)
{
    /* TODO: */
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
        SDL_CancelGPUCommandBuffer(commandBuffer);
        return;
    }
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize.x = width;
    io.DisplaySize.y = height;
    ImGui_ImplSDLGPU3_NewFrame();
    ImGui::NewFrame();
    /* TODO: */
    ImGui::Render();
    ImGui_ImplSDLGPU3_PrepareDrawData(ImGui::GetDrawData(), commandBuffer);
    RenderVoxel(commandBuffer);
    Letterbox(commandBuffer, swapchainTexture);
    {
        SDL_GPUColorTargetInfo info{};
        info.texture = swapchainTexture;
        info.load_op = SDL_GPU_LOADOP_LOAD;
        info.store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPURenderPass* renderPass = SDL_BeginGPURenderPass(commandBuffer, &info, 1, nullptr);
        if (!renderPass)
        {
            SDL_Log("Failed to begin render pass: %s", SDL_GetError());
            SDL_SubmitGPUCommandBuffer(commandBuffer);
            return;
        }
        ImGui_ImplSDLGPU3_RenderDrawData(ImGui::GetDrawData(), commandBuffer, renderPass);
        SDL_EndGPURenderPass(renderPass);
    }
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
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL3_ProcessEvent(&event);
            switch (event.type)
            {
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
    SDL_ReleaseGPUTexture(device, colorTexture);
    SDL_ReleaseGPUTexture(device, depthTexture);
    SDL_ReleaseGPUGraphicsPipeline(device, voxelPipeline);
    ImGui_ImplSDLGPU3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_ReleaseWindowFromGPUDevice(device, window);
    SDL_DestroyGPUDevice(device);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}