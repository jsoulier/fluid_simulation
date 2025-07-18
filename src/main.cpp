#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <exception>
#include <format>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

#include "config.hpp"
#include "debug_group.hpp"
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

struct Spawner
{
    Texture texture;
    int position[3];
    float value;
};

void to_json(nlohmann::json& json, const Spawner& spawner)
{
    json["texture"] = spawner.texture;
    json["position"] = spawner.position;
    json["value"] = spawner.value;
}

void from_json(const nlohmann::json& json, Spawner& spawner)
{
    spawner.texture = json["texture"];
    spawner.position[0] = json["position"][0];
    spawner.position[1] = json["position"][1];
    spawner.position[2] = json["position"][2];
    spawner.value = json["value"];
}

struct State
{
    int size = 128;
    int iterations = 5;
    float diffusion = 0.01f;
    float viscosity = 0.01f;
    std::vector<Spawner> spawners;
};

void to_json(nlohmann::json& json, const State& state)
{
    json["size"] = state.size;
    json["iterations"] = state.iterations;
    json["diffusion"] = state.diffusion;
    json["viscosity"] = state.viscosity;
    json["spawners"] = state.spawners;
}

void from_json(const nlohmann::json& json, State& state)
{
    state.size = json["size"];
    state.iterations = json["iterations"];
    state.diffusion = json["diffusion"];
    state.viscosity = json["viscosity"];
    state.spawners = json["spawners"];
}

static_assert(TextureVelocityX == 0);
static_assert(TextureVelocityY == 1);
static_assert(TextureVelocityZ == 2);

static constexpr const char* Textures[] =
{
    "Velocity (X)",
    "Velocity (Y)",
    "Velocity (Z)",
    "Pressure",
    "Divergence",
    "Density",
    "Combined",
};

static constexpr Texture Spawners[] =
{
    TextureVelocityX,
    TextureVelocityY,
    TextureVelocityZ,
    TextureDensity
};

static constexpr int Width = 960;
static constexpr int Height = 720;
static constexpr float Zoom = 1.0f;
static constexpr float Pan = 0.0002f;
static constexpr float Fov = glm::radians(60.0f);
static constexpr float Near = 0.1f;
static constexpr float Far = 1000.0f;

static SDL_Window* window;
static SDL_GPUDevice* device;
static SDL_GPUTexture* colorTexture;
static uint32_t width;
static uint32_t height;
static ReadWriteTexture textures[TextureCount];
static SDL_GPUSampler* sampler;
static float dt;
static int delay = 16;
static int cooldown;
static uint64_t time1;
static uint64_t time2;
static float pitch;
static float yaw;
static float distance = 200;
static glm::vec3 position;
static glm::mat4 view;
static glm::mat4 proj;
static glm::mat4 inverseView;
static glm::mat4 inverseProj;
static glm::mat4 viewProj;
static int texture = TextureCount;
static bool focused;
static State state;
static std::mutex mutex;

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
/* NOTE: forcing Vulkan when debugging on Windows since debug groups are broken on D3D12 */
#ifndef NDEBUG
    device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, true, nullptr);
#else
    device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_DXIL, true, nullptr);
#endif
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

static bool CreateSamplers()
{
    SDL_GPUSamplerCreateInfo info{};
    info.min_filter = SDL_GPU_FILTER_NEAREST;
    info.mag_filter = SDL_GPU_FILTER_NEAREST;
    info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler = SDL_CreateGPUSampler(device, &info);
    if (!sampler)
    {
        SDL_Log("Failed to create sampler: %s", SDL_GetError());
        return false;
    }
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
    return true;
}

static void UpdateViewProj()
{
    glm::vec3 vector;
    vector.x = std::cos(pitch) * std::cos(yaw);
    vector.y = std::sin(pitch);
    vector.z = std::cos(pitch) * std::sin(yaw);
    float ratio = static_cast<float>(Width) / Height;
    glm::vec3 center = glm::vec3(state.size / 2);
    position = center - vector * distance;
    view = glm::lookAt(position, position + vector, {0.0f, 1.0f, 0.0f});
    proj = glm::perspective(Fov, ratio, Near, Far);
    inverseView = glm::inverse(view);
    inverseProj = glm::inverse(proj);
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
    int groups = (state.size + THREADS_3D - 1) / THREADS_3D;
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
    int groups = (state.size + THREADS_3D - 1) / THREADS_3D;
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
        if (!textures[i].Create(device, state.size))
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

static void SaveCallback(void *userdata, const char* const* filelist, int filter)
{
    if (!filelist || !filelist[0])
    {
        return;
    }
    std::ofstream file(filelist[0]);
    if (!file)
    {
        SDL_Log("Failed to open file: %s", filelist[0]);
        return;
    }
    std::lock_guard lock(mutex);
    try
    {
        nlohmann::json json = state;
        file << json.dump(4);
    }
    catch (const std::exception& exception)
    {
        SDL_Log("Failed to save json: %s, %s", filelist[0], exception.what());
    }
}

static void LoadCallback(void *userdata, const char* const* filelist, int filter)
{
    if (!filelist || !filelist[0])
    {
        return;
    }
    std::ifstream file(filelist[0]);
    if (!file)
    {
        SDL_Log("Failed to open file: %s", filelist[0]);
        return;
    }
    std::lock_guard lock(mutex);
    nlohmann::json json;
    try
    {
        file >> json;
    }
    catch (const std::exception& exception)
    {
        SDL_Log("Failed to load json: %s, %s", filelist[0], exception.what());
        return;
    }
    state = json;
    CreateCells();
}

static void UpdateSpawners(SDL_GPUCommandBuffer* commandBuffer)
{
    std::vector<int> removes;
    for (int i = 0; i < state.spawners.size(); i++)
    {
        std::string removeId = std::format("Remove##remove{}", i);
        std::string positionId = std::format("##position{}", i);
        std::string valueId = std::format("##value{}", i);
        std::string textureId = std::format("##texture{}", i);
        Spawner& spawner = state.spawners[i];
        ImGui::SliderInt3(positionId.data(), spawner.position, 1, state.size - 2);
        ImGui::DragFloat(valueId.data(), &spawner.value, 1.0f);
        if (ImGui::BeginCombo(textureId.data(), Textures[spawner.texture]))
        {
            for (int j = 0; j < SDL_arraysize(Spawners); j++)
            {
                bool isSelected = spawner.texture == j;
                if (ImGui::Selectable(Textures[Spawners[j]], isSelected))
                {
                    spawner.texture = Spawners[j];
                }
                if (isSelected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        if (ImGui::Button(removeId.data()))
        {
            removes.push_back(i);
        }
        const int x = spawner.position[0];
        const int y = spawner.position[1];
        const int z = spawner.position[2];
        Add1(commandBuffer, spawner.texture, {x, y, z}, spawner.value);
        ImGui::Separator();
    }
    for (auto it = removes.rbegin(); it != removes.rend(); it++)
    {
        state.spawners.erase(state.spawners.begin() + *it);
    }
    if (ImGui::Button("Add##Spawner"))
    {
        Spawner spawner{};
        spawner.texture = TextureDensity;
        spawner.value = 1.0f;
        int center = state.size / 2 - 1;
        spawner.position[0] = center;
        spawner.position[1] = center;
        spawner.position[2] = center;
        state.spawners.push_back(spawner);
    }
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
    const char* location = SDL_GetCurrentDirectory();
    if (ImGui::Button("Save"))
    {
        SDL_ShowSaveFileDialog(SaveCallback, nullptr, window, nullptr, 1, location);
    }
    ImGui::SameLine();
    if (ImGui::Button("Load"))
    {
        SDL_ShowOpenFileDialog(LoadCallback, nullptr, window, nullptr, 1, location, false);
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset"))
    {
        CreateCells();
    }
    ImGui::SeparatorText("Settings");
    ImGui::SliderInt("Delay", &delay, 0, 1000);
    ImGui::SliderInt("Iterations", &state.iterations, 1, 50);
    ImGui::SliderFloat("Diffusion", &state.diffusion, 0.0f, 1.0f);
    ImGui::SliderFloat("Viscosity", &state.viscosity, 0.0f, 1.0f);
    if (ImGui::SliderInt("Size", &state.size, 16, 256))
    {
        CreateCells();
    }
    ImGui::SeparatorText("Viewer");
    for (int i = 0; i < SDL_arraysize(Textures); i++)
    {
        ImGui::RadioButton(Textures[i], &texture, i);
    }
    ImGui::SeparatorText("Spawners");
    UpdateSpawners(commandBuffer);
    focused = ImGui::IsWindowFocused();
    ImGui::End();
    ImGui::Render();
    ImGui_ImplSDLGPU3_PrepareDrawData(ImGui::GetDrawData(), commandBuffer);
}

static void Diffuse(SDL_GPUCommandBuffer* commandBuffer, ReadWriteTexture& texture, float diffusion)
{
    DEBUG_GROUP(device, commandBuffer);
    SDL_GPUComputePass* computePass = texture.BeginWritePass(commandBuffer);
    if (!computePass)
    {
        SDL_Log("Failed to begin compute pass: %s", SDL_GetError());
        return;
    }
    SDL_GPUTextureSamplerBinding textureBindings{};
    textureBindings.sampler = sampler;
    textureBindings.texture = texture.GetReadTexture();
    int groups = (state.size + THREADS_3D - 1) / THREADS_3D;
    BindPipeline(computePass, ComputePipelineTypeDiffuse);
    SDL_BindGPUComputeSamplers(computePass, 0, &textureBindings, 1);
    SDL_PushGPUComputeUniformData(commandBuffer, 0, &dt, sizeof(dt));
    SDL_PushGPUComputeUniformData(commandBuffer, 1, &diffusion, sizeof(diffusion));
    SDL_DispatchGPUCompute(computePass, groups, groups, groups);
    SDL_EndGPUComputePass(computePass);
    texture.Swap();
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
    SDL_GPUTextureSamplerBinding textureBindings[3]{};
    textureBindings[0].sampler = sampler;
    textureBindings[0].texture = textures[TextureVelocityX].GetReadTexture();
    textureBindings[1].sampler = sampler;
    textureBindings[1].texture = textures[TextureVelocityY].GetReadTexture();
    textureBindings[2].sampler = sampler;
    textureBindings[2].texture = textures[TextureVelocityZ].GetReadTexture();
    int groups = (state.size + THREADS_3D - 1) / THREADS_3D;
    BindPipeline(computePass, ComputePipelineTypeProject1);
    SDL_BindGPUComputeSamplers(computePass, 0, textureBindings, 3);
    SDL_DispatchGPUCompute(computePass, groups, groups, groups);
    SDL_EndGPUComputePass(computePass);
    textures[TexturePressure].Swap();
    textures[TextureDivergence].Swap();
}

static void Project2(SDL_GPUCommandBuffer* commandBuffer)
{
    DEBUG_GROUP(device, commandBuffer);
    SDL_GPUComputePass* computePass = textures[TexturePressure].BeginWritePass(commandBuffer);
    if (!computePass)
    {
        SDL_Log("Failed to begin compute pass: %s", SDL_GetError());
        return;
    }
    SDL_GPUTextureSamplerBinding textureBinding{};
    textureBinding.sampler = sampler;
    textureBinding.texture = textures[TextureDivergence].GetReadTexture();
    int groups = (state.size + THREADS_3D - 1) / THREADS_3D;
    BindPipeline(computePass, ComputePipelineTypeProject2);
    SDL_BindGPUComputeSamplers(computePass, 0, &textureBinding, 1);
    SDL_DispatchGPUCompute(computePass, groups, groups, groups);
    SDL_EndGPUComputePass(computePass);
    textures[TexturePressure].Swap();
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
    SDL_GPUTextureSamplerBinding textureBindings[4]{};
    textureBindings[0].sampler = sampler;
    textureBindings[0].texture = textures[TexturePressure].GetReadTexture();
    textureBindings[1].sampler = sampler;
    textureBindings[1].texture = textures[TextureVelocityX].GetReadTexture();
    textureBindings[2].sampler = sampler;
    textureBindings[2].texture = textures[TextureVelocityY].GetReadTexture();
    textureBindings[3].sampler = sampler;
    textureBindings[3].texture = textures[TextureVelocityZ].GetReadTexture();
    int groups = (state.size + THREADS_3D - 1) / THREADS_3D;
    BindPipeline(computePass, ComputePipelineTypeProject3);
    SDL_BindGPUComputeSamplers(computePass, 0, textureBindings, 4);
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
    SDL_GPUTextureSamplerBinding textureBindings[3]{};
    textureBindings[0].sampler = sampler;
    textureBindings[0].texture = textures[TextureVelocityX].GetReadTexture();
    textureBindings[1].sampler = sampler;
    textureBindings[1].texture = textures[TextureVelocityY].GetReadTexture();
    textureBindings[2].sampler = sampler;
    textureBindings[2].texture = textures[TextureVelocityZ].GetReadTexture();
    int groups = (state.size + THREADS_3D - 1) / THREADS_3D;
    BindPipeline(computePass, ComputePipelineTypeAdvect1);
    SDL_BindGPUComputeSamplers(computePass, 0, textureBindings, 3);
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
    SDL_GPUTextureSamplerBinding textureBindings[4]{};
    textureBindings[0].sampler = sampler;
    textureBindings[0].texture = textures[TextureDensity].GetReadTexture();
    textureBindings[1].sampler = sampler;
    textureBindings[1].texture = textures[TextureVelocityX].GetReadTexture();
    textureBindings[2].sampler = sampler;
    textureBindings[2].texture = textures[TextureVelocityY].GetReadTexture();
    textureBindings[3].sampler = sampler;
    textureBindings[3].texture = textures[TextureVelocityZ].GetReadTexture();
    int groups = (state.size + THREADS_3D - 1) / THREADS_3D;
    BindPipeline(computePass, ComputePipelineTypeAdvect2);
    SDL_BindGPUComputeSamplers(computePass, 0, textureBindings, 4);
    SDL_PushGPUComputeUniformData(commandBuffer, 0, &dt, sizeof(dt));
    SDL_DispatchGPUCompute(computePass, groups, groups, groups);
    SDL_EndGPUComputePass(computePass);
    textures[TextureDensity].Swap();
}

static void SetBnd1(SDL_GPUCommandBuffer* commandBuffer, ReadWriteTexture& texture, int type)
{
    DEBUG_GROUP(device, commandBuffer);
    SDL_GPUComputePass* computePass = texture.BeginWritePass(commandBuffer);
    if (!computePass)
    {
        SDL_Log("Failed to begin compute pass: %s", SDL_GetError());
        return;
    }
    SDL_GPUTextureSamplerBinding textureBindings{};
    textureBindings.sampler = sampler;
    textureBindings.texture = texture.GetReadTexture();
    int groups = (state.size + THREADS_3D - 1) / THREADS_3D;
    BindPipeline(computePass, ComputePipelineTypeSetBnd1);
    SDL_BindGPUComputeSamplers(computePass, 0, &textureBindings, 1);
    SDL_PushGPUComputeUniformData(commandBuffer, 0, &type, sizeof(type));
    SDL_DispatchGPUCompute(computePass, groups, groups, 2);
    SDL_EndGPUComputePass(computePass);
}

static void SetBnd2(SDL_GPUCommandBuffer* commandBuffer, ReadWriteTexture& texture, int type)
{
    DEBUG_GROUP(device, commandBuffer);
    SDL_GPUComputePass* computePass = texture.BeginWritePass(commandBuffer);
    if (!computePass)
    {
        SDL_Log("Failed to begin compute pass: %s", SDL_GetError());
        return;
    }
    SDL_GPUTextureSamplerBinding textureBindings{};
    textureBindings.sampler = sampler;
    textureBindings.texture = texture.GetReadTexture();
    int groups = (state.size + THREADS_3D - 1) / THREADS_3D;
    BindPipeline(computePass, ComputePipelineTypeSetBnd2);
    SDL_BindGPUComputeSamplers(computePass, 0, &textureBindings, 1);
    SDL_PushGPUComputeUniformData(commandBuffer, 0, &type, sizeof(type));
    SDL_DispatchGPUCompute(computePass, groups, 2, groups);
    SDL_EndGPUComputePass(computePass);
}

static void SetBnd3(SDL_GPUCommandBuffer* commandBuffer, ReadWriteTexture& texture, int type)
{
    DEBUG_GROUP(device, commandBuffer);
    SDL_GPUComputePass* computePass = texture.BeginWritePass(commandBuffer);
    if (!computePass)
    {
        SDL_Log("Failed to begin compute pass: %s", SDL_GetError());
        return;
    }
    SDL_GPUTextureSamplerBinding textureBindings{};
    textureBindings.sampler = sampler;
    textureBindings.texture = texture.GetReadTexture();
    int groups = (state.size + THREADS_3D - 1) / THREADS_3D;
    BindPipeline(computePass, ComputePipelineTypeSetBnd3);
    SDL_BindGPUComputeSamplers(computePass, 0, &textureBindings, 1);
    SDL_PushGPUComputeUniformData(commandBuffer, 0, &type, sizeof(type));
    SDL_DispatchGPUCompute(computePass, 2, groups, groups);
    SDL_EndGPUComputePass(computePass);
}

static void SetBnd4(SDL_GPUCommandBuffer* commandBuffer, ReadWriteTexture& texture)
{
    DEBUG_GROUP(device, commandBuffer);
    SDL_GPUComputePass* computePass = texture.BeginWritePass(commandBuffer);
    if (!computePass)
    {
        SDL_Log("Failed to begin compute pass: %s", SDL_GetError());
        return;
    }
    SDL_GPUTextureSamplerBinding textureBindings{};
    textureBindings.sampler = sampler;
    textureBindings.texture = texture.GetReadTexture();
    BindPipeline(computePass, ComputePipelineTypeSetBnd4);
    SDL_BindGPUComputeSamplers(computePass, 0, &textureBindings, 1);
    SDL_DispatchGPUCompute(computePass, 1, 1, 1);
    SDL_EndGPUComputePass(computePass);
}

static void SetBnd5(SDL_GPUCommandBuffer* commandBuffer, ReadWriteTexture& texture)
{
    DEBUG_GROUP(device, commandBuffer);
    SDL_GPUComputePass* computePass = texture.BeginWritePass(commandBuffer);
    if (!computePass)
    {
        SDL_Log("Failed to begin compute pass: %s", SDL_GetError());
        return;
    }
    SDL_GPUTextureSamplerBinding textureBindings{};
    textureBindings.sampler = sampler;
    textureBindings.texture = texture.GetReadTexture();
    int groups = (state.size + THREADS_3D - 1) / THREADS_3D;
    BindPipeline(computePass, ComputePipelineTypeSetBnd5);
    SDL_BindGPUComputeSamplers(computePass, 0, &textureBindings, 1);
    SDL_DispatchGPUCompute(computePass, groups, groups, groups);
    SDL_EndGPUComputePass(computePass);
}

/* TODO: i'm not really sure what set_bnd is supposed to do. it makes the border cells
sample the raw value (no weighting) from the neighboring cells. that's all. seems wrong */
static void SetBnd(SDL_GPUCommandBuffer* commandBuffer, ReadWriteTexture& texture, int type)
{
    /* TODO: only sides and corners are handled in the example (not edges) */
    // SetBnd1(commandBuffer, texture, type);
    // SetBnd2(commandBuffer, texture, type);
    // SetBnd3(commandBuffer, texture, type);
    // SetBnd4(commandBuffer, texture);
    // SetBnd5(commandBuffer, texture);
    // texture.Swap();
}

static void RenderCombined(SDL_GPUCommandBuffer* commandBuffer)
{
    DEBUG_GROUP(device, commandBuffer);
    SDL_GPUColorTargetInfo colorInfo{};
    colorInfo.texture = colorTexture;
    colorInfo.load_op = SDL_GPU_LOADOP_CLEAR;
    colorInfo.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass* renderPass = SDL_BeginGPURenderPass(commandBuffer, &colorInfo, 1, nullptr);
    if (!renderPass)
    {
        SDL_Log("Failed to begin render pass: %s", SDL_GetError());
        return;
    }
    SDL_GPUTextureSamplerBinding textureBindings[4]{};
    textureBindings[0].sampler = sampler;
    textureBindings[0].texture = textures[TextureVelocityX].GetReadTexture();
    textureBindings[1].sampler = sampler;
    textureBindings[1].texture = textures[TextureVelocityY].GetReadTexture();
    textureBindings[2].sampler = sampler;
    textureBindings[2].texture = textures[TextureVelocityZ].GetReadTexture();
    textureBindings[3].sampler = sampler;
    textureBindings[3].texture = textures[TextureDensity].GetReadTexture();
    BindPipeline(renderPass, GraphicsPipelineTypeCombined);
    SDL_BindGPUFragmentSamplers(renderPass, 0, textureBindings, 4);
    SDL_PushGPUFragmentUniformData(commandBuffer, 0, &inverseView, sizeof(inverseView));
    SDL_PushGPUFragmentUniformData(commandBuffer, 1, &inverseProj, sizeof(inverseProj));
    SDL_PushGPUFragmentUniformData(commandBuffer, 2, &position, sizeof(position));
    SDL_DrawGPUPrimitives(renderPass, 4, 1, 0, 0);
    SDL_EndGPURenderPass(renderPass);
}

static void RenderDebug(SDL_GPUCommandBuffer* commandBuffer)
{
    DEBUG_GROUP(device, commandBuffer);
    SDL_GPUColorTargetInfo colorInfo{};
    colorInfo.texture = colorTexture;
    colorInfo.load_op = SDL_GPU_LOADOP_CLEAR;
    colorInfo.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass* renderPass = SDL_BeginGPURenderPass(commandBuffer, &colorInfo, 1, nullptr);
    if (!renderPass)
    {
        SDL_Log("Failed to begin render pass: %s", SDL_GetError());
        return;
    }
    SDL_GPUTextureSamplerBinding textureBinding{};
    textureBinding.sampler = sampler;
    textureBinding.texture = textures[texture].GetReadTexture();
    BindPipeline(renderPass, GraphicsPipelineTypeDebug);
    SDL_BindGPUFragmentSamplers(renderPass, 0, &textureBinding, 1);
    SDL_PushGPUFragmentUniformData(commandBuffer, 0, &inverseView, sizeof(inverseView));
    SDL_PushGPUFragmentUniformData(commandBuffer, 1, &inverseProj, sizeof(inverseProj));
    SDL_PushGPUFragmentUniformData(commandBuffer, 2, &position, sizeof(position));
    SDL_DrawGPUPrimitives(renderPass, 4, 1, 0, 0);
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
        for (int i = 0; i < state.iterations; i++)
        {
            /* TODO: is viscosity correct here? */
            Diffuse(commandBuffer, textures[TextureVelocityX], state.viscosity);
            Diffuse(commandBuffer, textures[TextureVelocityY], state.viscosity);
            Diffuse(commandBuffer, textures[TextureVelocityZ], state.viscosity);
            SetBnd(commandBuffer, textures[TextureVelocityX], 1);
            SetBnd(commandBuffer, textures[TextureVelocityY], 2);
            SetBnd(commandBuffer, textures[TextureVelocityZ], 3);
        }
        Project1(commandBuffer);
        SetBnd(commandBuffer, textures[TextureDivergence], 0);
        SetBnd(commandBuffer, textures[TexturePressure], 0);
        for (int i = 0; i < state.iterations; i++)
        {
            Project2(commandBuffer);
            SetBnd(commandBuffer, textures[TexturePressure], 0);
        }
        Project3(commandBuffer);
        SetBnd(commandBuffer, textures[TextureVelocityX], 1);
        SetBnd(commandBuffer, textures[TextureVelocityY], 2);
        SetBnd(commandBuffer, textures[TextureVelocityZ], 3);
        Advect1(commandBuffer, TextureVelocityX);
        Advect1(commandBuffer, TextureVelocityY);
        Advect1(commandBuffer, TextureVelocityZ);
        textures[TextureVelocityX].Swap();
        textures[TextureVelocityY].Swap();
        textures[TextureVelocityZ].Swap();
        SetBnd(commandBuffer, textures[TextureVelocityX], 1);
        SetBnd(commandBuffer, textures[TextureVelocityY], 2);
        SetBnd(commandBuffer, textures[TextureVelocityZ], 3);
        Project1(commandBuffer);
        Project2(commandBuffer);
        Project3(commandBuffer);
        Diffuse(commandBuffer, textures[TextureDensity], state.diffusion);
        Advect2(commandBuffer);
        SetBnd(commandBuffer, textures[TextureDensity], 0);
        cooldown = delay;
    }
    if (texture == TextureCount)
    {
        RenderCombined(commandBuffer);
    }
    else
    {
        RenderDebug(commandBuffer);
    }
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
    if (!CreateSamplers())
    {
        SDL_Log("Failed to create samplers");
        return 1;
    }
    if (!CreateTextures())
    {
        SDL_Log("Failed to create textures");
        return 1;
    }
    if (argc > 1)
    {
        LoadCallback(nullptr, argv + 1, 0);
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
                    std::lock_guard lock(mutex);
                    CreateCells();
                }
                break;
            case SDL_EVENT_DROP_FILE:
                LoadCallback(nullptr, &event.drop.data, 0);
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
        std::lock_guard lock(mutex);
        Update();
    }
    SDL_HideWindow(window);
    for (int i = 0; i < TextureCount; i++)
    {
        textures[i].Free(device);
    }
    SDL_ReleaseGPUTexture(device, colorTexture);
    SDL_ReleaseGPUSampler(device, sampler);
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