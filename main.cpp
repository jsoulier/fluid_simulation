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
#include "debug_group.hpp"
#include "mesh.hpp"
#include "pipeline.hpp"
#include "rw_texture.hpp"
#include "shader.hpp"

enum Texture
{
    TextureVelocityX,
    TextureVelocityY,
    TextureVelocityZ,
    TexturePressure,
    TextureDivergence,
    TextureDensity,
    TextureCount,
};

static_assert(TextureVelocityX == 0);
static_assert(TextureVelocityY == 1);
static_assert(TextureVelocityZ == 2);

static constexpr int Width = 960;
static constexpr int Height = 720;
static constexpr float Zoom = 1.0f;
static constexpr float Pan = 0.0002f;
static constexpr float Fov = glm::radians(60.0f);
static constexpr float Near = 0.1f;
static constexpr float Far = 1000.0f;
static constexpr float Velocity = 5.0f;
static constexpr float Density = 5.0f;

static SDL_Window* window;
static SDL_GPUDevice* device;
static SDL_GPUTexture* colorTexture;
static SDL_GPUTexture* depthTexture;
static ReadWriteTexture textures[TextureCount];
static int size = 128;
static int iterations = 5;
static float dt;
static int delay = 16;
static int cooldown;
static uint64_t time1;
static uint64_t time2;
static float diffusion = 0.01f;
static float viscosity = 0.01f;
static uint32_t width;
static uint32_t height;
static glm::vec3 position;
static float pitch;
static float yaw;
static float distance = std::hypotf(size, size);
static glm::mat4 viewProj;
static int texture = TextureVelocityX;
static bool focused;

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

static void UpdateViewProj()
{
    static constexpr glm::vec3 Up{0.0f, 1.0f, 0.0f};
    float cosPitch = std::cos(pitch);
    glm::vec3 vector;
    vector.x = cosPitch * std::cos(yaw);
    vector.y = std::sin(pitch);
    vector.z = cosPitch * std::sin(yaw);
    float ratio = static_cast<float>(Width) / Height;
    glm::vec3 center = glm::vec3(size / 2);
    glm::vec3 position = center - vector * distance;
    glm::mat4 view = glm::lookAt(position, position + vector, Up);
    glm::mat4 proj = glm::perspective(Fov, ratio, Near, Far);
    viewProj = proj * view;
}

static void Add1(SDL_GPUCommandBuffer* commandBuffer, Texture texture, const glm::ivec3& position, float value)
{
    DEBUG_GROUP(device, commandBuffer);
    SDL_GPUComputePass* computePass = textures[texture].BeginReadPass(commandBuffer);
    if (!computePass)
    {
        SDL_Log("Failed to begin compute pass: %s", SDL_GetError());
        return;
    }
    BindPipeline(computePass, ComputePipelineTypeAdd1);
    SDL_PushGPUComputeUniformData(commandBuffer, 0, &position, sizeof(position));
    SDL_PushGPUComputeUniformData(commandBuffer, 1, &value, sizeof(value));
    SDL_DispatchGPUCompute(computePass, 1, 1, 1);
    SDL_EndGPUComputePass(computePass);
}

static void Add2(SDL_GPUCommandBuffer* commandBuffer, Texture texture, float value)
{
    DEBUG_GROUP(device, commandBuffer);
    SDL_GPUComputePass* computePass = textures[texture].BeginReadPass(commandBuffer);
    if (!computePass)
    {
        SDL_Log("Failed to begin compute pass: %s", SDL_GetError());
        return;
    }
    int groups = (size + THREADS_3D - 1) / THREADS_3D;
    BindPipeline(computePass, ComputePipelineTypeAdd2);
    SDL_PushGPUComputeUniformData(commandBuffer, 0, &value, sizeof(value));
    SDL_DispatchGPUCompute(computePass, groups, groups, groups);
    SDL_EndGPUComputePass(computePass);
}

static void Clear(SDL_GPUCommandBuffer* commandBuffer, ReadWriteTexture& texture, float value = 0.0f)
{
    DEBUG_GROUP(device, commandBuffer);
    SDL_GPUComputePass* computePass = texture.BeginWritePass(commandBuffer);
    if (!computePass)
    {
        SDL_Log("Failed to begin compute pass: %s", SDL_GetError());
        return;
    }
    int groups = (size + THREADS_3D - 1) / THREADS_3D;
    BindPipeline(computePass, ComputePipelineTypeClear);
    SDL_PushGPUComputeUniformData(commandBuffer, 0, &value, sizeof(value));
    SDL_DispatchGPUCompute(computePass, groups, groups, groups);
    SDL_EndGPUComputePass(computePass);
}

static bool CreateCells()
{
    SDL_GPUCommandBuffer* commandBuffer = SDL_AcquireGPUCommandBuffer(device);
    if (!commandBuffer)
    {
        SDL_Log("Failed to acquire command buffer: %s", SDL_GetError());
        return false;
    }
    for (int i = 0; i < TextureCount; i++)
    {
        if (!textures[i].Create(device, size))
        {
            SDL_Log("Failed to create texture: %d", i);
            return false;
        }
        Clear(commandBuffer, textures[i]);
        textures[i].Swap();
        Clear(commandBuffer, textures[i]);
    }
    SDL_SubmitGPUCommandBuffer(commandBuffer);
    return true;
}

static void UpdateImGui(SDL_GPUCommandBuffer* commandBuffer)
{
    DEBUG_GROUP(device, commandBuffer);
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize.x = width;
    io.DisplaySize.y = height;
    ImGui_ImplSDLGPU3_NewFrame();
    ImGui::NewFrame();
    ImGui::Begin("Fluid Simulation");
    ImGui::SliderInt("Delay", &delay, 0, 1000);
    ImGui::SliderInt("Iterations", &iterations, 1, 50);
    ImGui::SliderFloat("Diffusion", &diffusion, 0.0f, 1.0f);
    ImGui::SliderFloat("Viscosity", &viscosity, 0.0f, 1.0f);
    if (ImGui::SliderInt("Size", &size, 16, 128))
    {
        CreateCells();
    }
    ImGui::Separator();
    ImGui::RadioButton("Velocity Texture (X)", &texture, TextureVelocityX);
    ImGui::RadioButton("Velocity Texture (Y)", &texture, TextureVelocityY);
    ImGui::RadioButton("Velocity Texture (Z)", &texture, TextureVelocityZ);
    ImGui::RadioButton("Pressure Texture", &texture, TexturePressure);
    ImGui::RadioButton("Divergence Texture", &texture, TextureDivergence);
    ImGui::RadioButton("Density Texture", &texture, TextureDensity);
    ImGui::Separator();
    auto add1 = [commandBuffer](Texture texture, const int position[3], float value)
    {
        Add1(commandBuffer, texture, {position[0], position[1], position[2]}, value);
    };
    static int center = size / 2 - 1;
    static float allVelocity[3];
    if (ImGui::Button("Add Velocity (All)"))
    {
        Add2(commandBuffer, TextureVelocityX, allVelocity[0]);
        Add2(commandBuffer, TextureVelocityY, allVelocity[1]);
        Add2(commandBuffer, TextureVelocityZ, allVelocity[2]);
    }
    ImGui::SliderFloat3("Velocity##All", allVelocity, -Velocity, Velocity);
    ImGui::Separator();
    static float singleVelocity[3];
    static int singleVelocityPosition[3] = {center, center, center};
    if (ImGui::Button("Add Velocity (Single)"))
    {
        add1(TextureVelocityX, singleVelocityPosition, singleVelocity[0]);
        add1(TextureVelocityY, singleVelocityPosition, singleVelocity[1]);
        add1(TextureVelocityZ, singleVelocityPosition, singleVelocity[2]);
    }
    ImGui::SliderInt3("Position##Single", singleVelocityPosition, 1, size - 2);
    ImGui::SliderFloat3("Velocity##Single", singleVelocity, -Velocity, Velocity);
    ImGui::Separator();
    static float density;
    static int densityPosition[3] = {center, center, center};
    if (ImGui::Button("Add Density"))
    {
        add1(TextureDensity, densityPosition, density);
    }
    ImGui::SliderInt3("Position", densityPosition, 1, size - 2);
    ImGui::SliderFloat("Density", &density, 0.0f, Density);
    ImGui::Separator();
    if (ImGui::Button("Reset"))
    {
        CreateCells();
    }
    focused = ImGui::IsWindowFocused();
    ImGui::End();
    ImGui::Render();
    ImGui_ImplSDLGPU3_PrepareDrawData(ImGui::GetDrawData(), commandBuffer);
}

static void Diffuse(SDL_GPUCommandBuffer* commandBuffer, ReadWriteTexture& texture, float diffusion)
{
    DEBUG_GROUP(device, commandBuffer);
    for (int i = 0; i < iterations; i++)
    {
        SDL_GPUComputePass* computePass = texture.BeginWritePass(commandBuffer);
        if (!computePass)
        {
            SDL_Log("Failed to begin compute pass: %s", SDL_GetError());
            return;
        }
        int groups = (size + THREADS_3D - 1) / THREADS_3D;
        BindPipeline(computePass, ComputePipelineTypeDiffuse);
        SDL_BindGPUComputeStorageTextures(computePass, 0, texture.GetReadTextureAddress(), 1);
        SDL_PushGPUComputeUniformData(commandBuffer, 0, &dt, sizeof(dt));
        SDL_PushGPUComputeUniformData(commandBuffer, 1, &diffusion, sizeof(diffusion));
        SDL_DispatchGPUCompute(computePass, groups, groups, groups);
        SDL_EndGPUComputePass(computePass);
        texture.Swap();
    }
}

static void Project1(SDL_GPUCommandBuffer* commandBuffer)
{
    DEBUG_GROUP(device, commandBuffer);
    SDL_GPUStorageTextureReadWriteBinding readWriteTextureBindings[2]{};
    readWriteTextureBindings[0].texture = textures[TexturePressure].GetWriteTexture();
    readWriteTextureBindings[1].texture = textures[TextureDivergence].GetWriteTexture();
    SDL_GPUComputePass* computePass = SDL_BeginGPUComputePass(commandBuffer, readWriteTextureBindings, 2, nullptr, 0);
    if (!computePass)
    {
        SDL_Log("Failed to begin compute pass: %s", SDL_GetError());
        return;
    }
    int groups = (size + THREADS_3D - 1) / THREADS_3D;
    BindPipeline(computePass, ComputePipelineTypeProject1);
    SDL_BindGPUComputeStorageTextures(computePass, 0, textures[TextureVelocityX].GetReadTextureAddress(), 1);
    SDL_BindGPUComputeStorageTextures(computePass, 1, textures[TextureVelocityY].GetReadTextureAddress(), 1);
    SDL_BindGPUComputeStorageTextures(computePass, 2, textures[TextureVelocityZ].GetReadTextureAddress(), 1);
    SDL_DispatchGPUCompute(computePass, groups, groups, groups);
    SDL_EndGPUComputePass(computePass);
    textures[TexturePressure].Swap();
    textures[TextureDivergence].Swap();
}

static void Project2(SDL_GPUCommandBuffer* commandBuffer)
{
    DEBUG_GROUP(device, commandBuffer);
    for (int i = 0; i < iterations; i++)
    {
        SDL_GPUComputePass* computePass = textures[TexturePressure].BeginWritePass(commandBuffer);
        if (!computePass)
        {
            SDL_Log("Failed to begin compute pass: %s", SDL_GetError());
            return;
        }
        int groups = (size + THREADS_3D - 1) / THREADS_3D;
        BindPipeline(computePass, ComputePipelineTypeProject2);
        SDL_BindGPUComputeStorageTextures(computePass, 0, textures[TextureDivergence].GetReadTextureAddress(), 1);
        SDL_DispatchGPUCompute(computePass, groups, groups, groups);
        SDL_EndGPUComputePass(computePass);
        textures[TexturePressure].Swap();
    }
}

static void Project3(SDL_GPUCommandBuffer* commandBuffer)
{
    DEBUG_GROUP(device, commandBuffer);
    SDL_GPUStorageTextureReadWriteBinding readWriteTextureBindings[3]{};
    readWriteTextureBindings[0].texture = textures[TextureVelocityX].GetWriteTexture();
    readWriteTextureBindings[1].texture = textures[TextureVelocityY].GetWriteTexture();
    readWriteTextureBindings[2].texture = textures[TextureVelocityZ].GetWriteTexture();
    SDL_GPUComputePass* computePass = SDL_BeginGPUComputePass(commandBuffer, readWriteTextureBindings, 3, nullptr, 0);
    if (!computePass)
    {
        SDL_Log("Failed to begin compute pass: %s", SDL_GetError());
        return;
    }
    int groups = (size + THREADS_3D - 1) / THREADS_3D;
    BindPipeline(computePass, ComputePipelineTypeProject3);
    SDL_BindGPUComputeStorageTextures(computePass, 0, textures[TexturePressure].GetReadTextureAddress(), 1);
    SDL_BindGPUComputeStorageTextures(computePass, 1, textures[TextureVelocityX].GetReadTextureAddress(), 1);
    SDL_BindGPUComputeStorageTextures(computePass, 2, textures[TextureVelocityY].GetReadTextureAddress(), 1);
    SDL_BindGPUComputeStorageTextures(computePass, 3, textures[TextureVelocityZ].GetReadTextureAddress(), 1);
    SDL_DispatchGPUCompute(computePass, groups, groups, groups);
    SDL_EndGPUComputePass(computePass);
    textures[TextureVelocityX].Swap();
    textures[TextureVelocityY].Swap();
    textures[TextureVelocityZ].Swap();
}

static void Advect1(SDL_GPUCommandBuffer* commandBuffer, Texture texture)
{
    DEBUG_GROUP(device, commandBuffer);
    assert(texture == 0 || texture == 1 || texture == 2);
    SDL_GPUComputePass* computePass = textures[texture].BeginWritePass(commandBuffer);
    if (!computePass)
    {
        SDL_Log("Failed to begin compute pass: %s", SDL_GetError());
        return;
    }
    int groups = (size + THREADS_3D - 1) / THREADS_3D;
    BindPipeline(computePass, ComputePipelineTypeAdvect1);
    SDL_BindGPUComputeStorageTextures(computePass, 0, textures[TextureVelocityX].GetReadTextureAddress(), 1);
    SDL_BindGPUComputeStorageTextures(computePass, 1, textures[TextureVelocityY].GetReadTextureAddress(), 1);
    SDL_BindGPUComputeStorageTextures(computePass, 2, textures[TextureVelocityZ].GetReadTextureAddress(), 1);
    SDL_PushGPUComputeUniformData(commandBuffer, 0, &texture, sizeof(texture));
    SDL_PushGPUComputeUniformData(commandBuffer, 1, &dt, sizeof(dt));
    SDL_DispatchGPUCompute(computePass, groups, groups, groups);
    SDL_EndGPUComputePass(computePass);
}

static void Advect2(SDL_GPUCommandBuffer* commandBuffer)
{
    DEBUG_GROUP(device, commandBuffer);
    SDL_GPUComputePass* computePass = textures[TextureDensity].BeginWritePass(commandBuffer);
    if (!computePass)
    {
        SDL_Log("Failed to begin compute pass: %s", SDL_GetError());
        return;
    }
    int groups = (size + THREADS_3D - 1) / THREADS_3D;
    BindPipeline(computePass, ComputePipelineTypeAdvect2);
    SDL_BindGPUComputeStorageTextures(computePass, 0, textures[TextureDensity].GetReadTextureAddress(), 1);
    SDL_BindGPUComputeStorageTextures(computePass, 1, textures[TextureVelocityX].GetReadTextureAddress(), 1);
    SDL_BindGPUComputeStorageTextures(computePass, 2, textures[TextureVelocityY].GetReadTextureAddress(), 1);
    SDL_BindGPUComputeStorageTextures(computePass, 3, textures[TextureVelocityZ].GetReadTextureAddress(), 1);
    SDL_PushGPUComputeUniformData(commandBuffer, 0, &dt, sizeof(dt));
    SDL_DispatchGPUCompute(computePass, groups, groups, groups);
    SDL_EndGPUComputePass(computePass);
    textures[TextureDensity].Swap();
}

static void RenderOutline(SDL_GPUCommandBuffer* commandBuffer)
{
    DEBUG_GROUP(device, commandBuffer);
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
    BindPipeline(renderPass, GraphicsPipelineTypeOutline);
    SDL_PushGPUVertexUniformData(commandBuffer, 0, &viewProj, sizeof(viewProj));
    SDL_PushGPUVertexUniformData(commandBuffer, 1, &size, sizeof(size));
    RenderMesh(renderPass, MeshTypeLineCube, 1);
    SDL_EndGPURenderPass(renderPass);
}

static void RenderVoxel(SDL_GPUCommandBuffer* commandBuffer)
{
    DEBUG_GROUP(device, commandBuffer);
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
    BindPipeline(renderPass, GraphicsPipelineTypeVoxel);
    SDL_BindGPUVertexStorageTextures(renderPass, 0, textures[texture].GetReadTextureAddress(), 1);
    SDL_PushGPUVertexUniformData(commandBuffer, 0, &viewProj, sizeof(viewProj));
    RenderMesh(renderPass, MeshTypeTriangleCube, size * size * size);
    SDL_EndGPURenderPass(renderPass);
}

static void Letterbox(SDL_GPUCommandBuffer* commandBuffer, SDL_GPUTexture* swapchainTexture)
{
    DEBUG_GROUP(device, commandBuffer);
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

static void RenderImGui(SDL_GPUCommandBuffer* commandBuffer, SDL_GPUTexture* swapchainTexture)
{
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

static void Update()
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
    UpdateImGui(commandBuffer);
    UpdateViewProj();
    if (cooldown <= 0)
    {
        Diffuse(commandBuffer, textures[TextureVelocityX], viscosity);
        Diffuse(commandBuffer, textures[TextureVelocityY], viscosity);
        Diffuse(commandBuffer, textures[TextureVelocityZ], viscosity);
        Project1(commandBuffer);
        Project2(commandBuffer);
        Project3(commandBuffer);
        Advect1(commandBuffer, TextureVelocityX);
        Advect1(commandBuffer, TextureVelocityY);
        Advect1(commandBuffer, TextureVelocityZ);
        textures[TextureVelocityX].Swap();
        textures[TextureVelocityY].Swap();
        textures[TextureVelocityZ].Swap();
        Project1(commandBuffer);
        Project2(commandBuffer);
        Project3(commandBuffer);
        Diffuse(commandBuffer, textures[TextureDensity], diffusion);
        Advect2(commandBuffer);
        cooldown = delay;
    }
    RenderOutline(commandBuffer);
    RenderVoxel(commandBuffer);
    Letterbox(commandBuffer, swapchainTexture);
    RenderImGui(commandBuffer, swapchainTexture);
    SDL_SubmitGPUCommandBuffer(commandBuffer);
    SDL_WaitForGPUIdle(device);
}

int main(int argc, char** argv)
{
    if (!Init())
    {
        SDL_Log("Failed to initialize");
        return 1;
    }
    if (!CreatePipelines(device, window))
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
        cooldown -= dt;
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
                if (!focused && event.motion.state & (SDL_BUTTON_LMASK | SDL_BUTTON_RMASK))
                {
                    float limit = glm::pi<float>() / 2.0f - 0.01f;
                    yaw += event.motion.xrel * Pan * dt;
                    pitch -= event.motion.yrel * Pan * dt;
                    pitch = std::clamp(pitch, -limit, limit);
                }
                break;
            case SDL_EVENT_KEY_DOWN:
                if (event.key.scancode == SDL_SCANCODE_R)
                {
                    CreateCells();
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
        Update();
    }
    SDL_HideWindow(window);
    for (int i = 0; i < TextureCount; i++)
    {
        textures[i].Free(device);
    }
    SDL_ReleaseGPUTexture(device, colorTexture);
    SDL_ReleaseGPUTexture(device, depthTexture);
    FreeMeshes(device);
    FreePipelines(device);
    ImGui_ImplSDLGPU3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_ReleaseWindowFromGPUDevice(device, window);
    SDL_DestroyGPUDevice(device);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}