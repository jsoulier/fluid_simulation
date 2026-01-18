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
#include "helpers.hpp"
#include "texture.hpp"

enum TextureType
{
    TextureTypeVelocityX,
    TextureTypeVelocityY,
    TextureTypeVelocityZ,
    TextureTypePressure,
    TextureTypeDivergence,
    TextureTypeDensity,
    TextureTypeCount,
};

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

static constexpr TextureType Spawners[] =
{
    TextureTypeVelocityX,
    TextureTypeVelocityY,
    TextureTypeVelocityZ,
    TextureTypeDensity
};

struct Spawner
{
    TextureType Texture;
    int Position[3];
    float Value;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(Spawner, Texture, Position, Value)
};

struct State
{
    int Size = 128;
    int Iterations = 5;
    float Diffusion = 0.01f;
    float Viscosity = 0.01f;
    std::vector<Spawner> Spawners;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(State, Size, Iterations, Diffusion, Viscosity, Spawners)
};

enum PipelineType
{
    PipelineTypeAdd1,
    PipelineTypeAdd2,
    PipelineTypeClear,
    PipelineTypeDiffuse,
    PipelineTypeProject1,
    PipelineTypeProject2,
    PipelineTypeProject3,
    PipelineTypeAdvect1,
    PipelineTypeAdvect2,
    PipelineTypeBnd1,
    PipelineTypeBnd2,
    PipelineTypeBnd3,
    PipelineTypeBnd4,
    PipelineTypeBnd5,
    PipelineTypeComposite,
    PipelineTypeSingle,
    PipelineTypeCount,
};

static constexpr int kWidth = 480;
static constexpr int kHeight = 360;
static constexpr float kZoom = 20.0f;
static constexpr float kPan = 0.005f;
static constexpr float kFov = glm::radians(60.0f);
static constexpr float kNear = 0.1f;
static constexpr float kFar = 1000.0f;
static constexpr float kCooldown = 16.0f;

static SDL_Window* window;
static SDL_GPUDevice* device;
static SDL_GPUComputePipeline* pipelines[PipelineTypeCount];
static SDL_GPUTexture* colorTexture;
static uint32_t width;
static uint32_t height;
static ReadWriteTexture textures[TextureTypeCount];
static SDL_GPUSampler* sampler;
static float speed = 16.0f;
static int cooldown;
static uint64_t time1;
static uint64_t time2;
static float pitch;
static float yaw;
static float distance = 200.0f;
static glm::vec3 position;
static glm::mat4 view;
static glm::mat4 proj;
static glm::mat4 inverseView;
static glm::mat4 inverseProj;
static glm::mat4 viewProj;
static int texture = TextureTypeCount;
static bool focused;
static bool hovered;
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
#ifndef NDEBUG
    device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL, true, nullptr);
#else
    device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL, false, nullptr);
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
    info.width = kWidth;
    info.height = kHeight;
    info.layer_count_or_depth = 1;
    info.num_levels = 1;
    info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    info.usage = SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    colorTexture = SDL_CreateGPUTexture(device, &info);
    if (!colorTexture)
    {
        SDL_Log("Failed to create texture: %s", SDL_GetError());
        return false;
    }
    return true;
}

bool CreatePipelines()
{
    pipelines[PipelineTypeAdd1] = LoadComputePipeline(device, "add1.comp");
    pipelines[PipelineTypeAdd2] = LoadComputePipeline(device, "add2.comp");
    pipelines[PipelineTypeClear] = LoadComputePipeline(device, "clear.comp");
    pipelines[PipelineTypeDiffuse] = LoadComputePipeline(device, "diffuse.comp");
    pipelines[PipelineTypeProject1] = LoadComputePipeline(device, "project1.comp");
    pipelines[PipelineTypeProject2] = LoadComputePipeline(device, "project2.comp");
    pipelines[PipelineTypeProject3] = LoadComputePipeline(device, "project3.comp");
    pipelines[PipelineTypeAdvect1] = LoadComputePipeline(device, "advect1.comp");
    pipelines[PipelineTypeAdvect2] = LoadComputePipeline(device, "advect2.comp");
    pipelines[PipelineTypeBnd1] = LoadComputePipeline(device, "bnd1.comp");
    pipelines[PipelineTypeBnd2] = LoadComputePipeline(device, "bnd2.comp");
    pipelines[PipelineTypeBnd3] = LoadComputePipeline(device, "bnd3.comp");
    pipelines[PipelineTypeBnd4] = LoadComputePipeline(device, "bnd4.comp");
    pipelines[PipelineTypeBnd5] = LoadComputePipeline(device, "bnd5.comp");
    pipelines[PipelineTypeComposite] = LoadComputePipeline(device, "composite.comp");
    pipelines[PipelineTypeSingle] = LoadComputePipeline(device, "single.comp");
    for (int i = PipelineTypeCount - 1; i >= 0; i--)
    {
        if (!pipelines[i])
        {
            SDL_Log("Failed to create compute pipeline: %d", i);
            return false;
        }
    }
    return true;
}

static void UpdateViewProj()
{
    glm::vec3 vector;
    vector.x = std::cos(pitch) * std::cos(yaw);
    vector.y = std::sin(pitch);
    vector.z = std::cos(pitch) * std::sin(yaw);
    float ratio = static_cast<float>(kWidth) / kHeight;
    glm::vec3 center = glm::vec3(state.Size / 2);
    position = center - vector * distance;
    view = glm::lookAt(position, position + vector, {0.0f, 1.0f, 0.0f});
    proj = glm::perspective(kFov, ratio, kNear, kFar);
    inverseView = glm::inverse(view);
    inverseProj = glm::inverse(proj);
    viewProj = proj * view;
}

static void Add1(SDL_GPUCommandBuffer* commandBuffer, TextureType texture, const glm::ivec3& position, float value)
{
    DebugGroup(commandBuffer);
    SDL_GPUComputePass* computePass = textures[texture].BeginReadPass(commandBuffer);
    if (!computePass)
    {
        SDL_Log("Failed to begin compute pass: %s", SDL_GetError());
        return;
    }
    SDL_BindGPUComputePipeline(computePass, pipelines[PipelineTypeAdd1]);
    SDL_PushGPUComputeUniformData(commandBuffer, 0, &position, sizeof(position));
    SDL_PushGPUComputeUniformData(commandBuffer, 1, &value, sizeof(value));
    SDL_DispatchGPUCompute(computePass, 1, 1, 1);
    SDL_EndGPUComputePass(computePass);
}

static void Add2(SDL_GPUCommandBuffer* commandBuffer, TextureType texture, float value)
{
    DebugGroup(commandBuffer);
    SDL_GPUComputePass* computePass = textures[texture].BeginReadPass(commandBuffer);
    if (!computePass)
    {
        SDL_Log("Failed to begin compute pass: %s", SDL_GetError());
        return;
    }
    int groups = (state.Size + THREADS - 1) / THREADS;
    SDL_BindGPUComputePipeline(computePass, pipelines[PipelineTypeAdd2]);
    SDL_PushGPUComputeUniformData(commandBuffer, 0, &value, sizeof(value));
    SDL_DispatchGPUCompute(computePass, groups, groups, groups);
    SDL_EndGPUComputePass(computePass);
}

static void Clear(SDL_GPUCommandBuffer* commandBuffer, ReadWriteTexture& texture, float value = 0.0f)
{
    DebugGroup(commandBuffer);
    SDL_GPUComputePass* computePass = texture.BeginWritePass(commandBuffer);
    if (!computePass)
    {
        SDL_Log("Failed to begin compute pass: %s", SDL_GetError());
        return;
    }
    int groups = (state.Size + THREADS - 1) / THREADS;
    SDL_BindGPUComputePipeline(computePass, pipelines[PipelineTypeClear]);
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
    for (int i = 0; i < TextureTypeCount; i++)
    {
        if (!textures[i].Create(device, state.Size))
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
    for (int i = 0; i < state.Spawners.size(); i++)
    {
        std::string removeId = std::format("Remove##remove{}", i);
        std::string positionId = std::format("##position{}", i);
        std::string valueId = std::format("##value{}", i);
        std::string textureId = std::format("##texture{}", i);
        Spawner& spawner = state.Spawners[i];
        ImGui::SliderInt3(positionId.data(), spawner.Position, 1, state.Size - 2);
        ImGui::DragFloat(valueId.data(), &spawner.Value, 1.0f);
        if (ImGui::BeginCombo(textureId.data(), Textures[spawner.Texture]))
        {
            for (int j = 0; j < SDL_arraysize(Spawners); j++)
            {
                bool isSelected = spawner.Texture == j;
                if (ImGui::Selectable(Textures[Spawners[j]], isSelected))
                {
                    spawner.Texture = Spawners[j];
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
        int x = spawner.Position[0];
        int y = spawner.Position[1];
        int z = spawner.Position[2];
        Add1(commandBuffer, spawner.Texture, {x, y, z}, spawner.Value);
        ImGui::Separator();
    }
    for (auto it = removes.rbegin(); it != removes.rend(); it++)
    {
        state.Spawners.erase(state.Spawners.begin() + *it);
    }
    if (ImGui::Button("Add##Spawner"))
    {
        Spawner spawner{};
        spawner.Texture = TextureTypeDensity;
        spawner.Value = 1.0f;
        int center = state.Size / 2 - 1;
        spawner.Position[0] = center;
        spawner.Position[1] = center;
        spawner.Position[2] = center;
        state.Spawners.push_back(spawner);
    }
}

static void UpdateImGui(SDL_GPUCommandBuffer* commandBuffer)
{
    DebugGroup(commandBuffer);
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
    ImGui::SliderFloat("Speed", &speed, 0.0f, 64.0f);
    ImGui::SliderInt("Iterations", &state.Iterations, 1, 50);
    ImGui::SliderFloat("Diffusion", &state.Diffusion, 0.0f, 1.0f);
    ImGui::SliderFloat("Viscosity", &state.Viscosity, 0.0f, 1.0f);
    if (ImGui::SliderInt("Size", &state.Size, 16, 256))
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
    hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    focused = ImGui::IsWindowFocused();
    ImGui::End();
    ImGui::Render();
    ImGui_ImplSDLGPU3_PrepareDrawData(ImGui::GetDrawData(), commandBuffer);
}

static void Diffuse(SDL_GPUCommandBuffer* commandBuffer, ReadWriteTexture& texture, float diffusion)
{
    DebugGroup(commandBuffer);
    SDL_GPUComputePass* computePass = texture.BeginWritePass(commandBuffer);
    if (!computePass)
    {
        SDL_Log("Failed to begin compute pass: %s", SDL_GetError());
        return;
    }
    SDL_GPUTextureSamplerBinding textureBindings{};
    textureBindings.sampler = sampler;
    textureBindings.texture = texture.GetReadTexture();
    int groups = (state.Size + THREADS - 1) / THREADS;
    SDL_BindGPUComputePipeline(computePass, pipelines[PipelineTypeDiffuse]);
    SDL_BindGPUComputeSamplers(computePass, 0, &textureBindings, 1);
    SDL_PushGPUComputeUniformData(commandBuffer, 0, &speed, sizeof(speed));
    SDL_PushGPUComputeUniformData(commandBuffer, 1, &diffusion, sizeof(diffusion));
    SDL_DispatchGPUCompute(computePass, groups, groups, groups);
    SDL_EndGPUComputePass(computePass);
    texture.Swap();
}

static void Project1(SDL_GPUCommandBuffer* commandBuffer)
{
    DebugGroup(commandBuffer);
    SDL_GPUStorageTextureReadWriteBinding readWriteTextureBindings[2]{};
    readWriteTextureBindings[0].texture = textures[TextureTypePressure].GetWriteTexture();
    readWriteTextureBindings[1].texture = textures[TextureTypeDivergence].GetWriteTexture();
    SDL_GPUComputePass* computePass = SDL_BeginGPUComputePass(commandBuffer, readWriteTextureBindings, 2, nullptr, 0);
    if (!computePass)
    {
        SDL_Log("Failed to begin compute pass: %s", SDL_GetError());
        return;
    }
    SDL_GPUTextureSamplerBinding textureBindings[3]{};
    textureBindings[0].sampler = sampler;
    textureBindings[0].texture = textures[TextureTypeVelocityX].GetReadTexture();
    textureBindings[1].sampler = sampler;
    textureBindings[1].texture = textures[TextureTypeVelocityY].GetReadTexture();
    textureBindings[2].sampler = sampler;
    textureBindings[2].texture = textures[TextureTypeVelocityZ].GetReadTexture();
    int groups = (state.Size + THREADS - 1) / THREADS;
    SDL_BindGPUComputePipeline(computePass, pipelines[PipelineTypeProject1]);
    SDL_BindGPUComputeSamplers(computePass, 0, textureBindings, 3);
    SDL_DispatchGPUCompute(computePass, groups, groups, groups);
    SDL_EndGPUComputePass(computePass);
    textures[TextureTypePressure].Swap();
    textures[TextureTypeDivergence].Swap();
}

static void Project2(SDL_GPUCommandBuffer* commandBuffer)
{
    DebugGroup(commandBuffer);
    SDL_GPUComputePass* computePass = textures[TextureTypePressure].BeginWritePass(commandBuffer);
    if (!computePass)
    {
        SDL_Log("Failed to begin compute pass: %s", SDL_GetError());
        return;
    }
    SDL_GPUTextureSamplerBinding textureBinding{};
    textureBinding.sampler = sampler;
    textureBinding.texture = textures[TextureTypeDivergence].GetReadTexture();
    int groups = (state.Size + THREADS - 1) / THREADS;
    SDL_BindGPUComputePipeline(computePass, pipelines[PipelineTypeProject2]);
    SDL_BindGPUComputeSamplers(computePass, 0, &textureBinding, 1);
    SDL_DispatchGPUCompute(computePass, groups, groups, groups);
    SDL_EndGPUComputePass(computePass);
    textures[TextureTypePressure].Swap();
}

static void Project3(SDL_GPUCommandBuffer* commandBuffer)
{
    DebugGroup(commandBuffer);
    SDL_GPUStorageTextureReadWriteBinding readWriteTextureBindings[3]{};
    readWriteTextureBindings[0].texture = textures[TextureTypeVelocityX].GetWriteTexture();
    readWriteTextureBindings[1].texture = textures[TextureTypeVelocityY].GetWriteTexture();
    readWriteTextureBindings[2].texture = textures[TextureTypeVelocityZ].GetWriteTexture();
    SDL_GPUComputePass* computePass = SDL_BeginGPUComputePass(commandBuffer, readWriteTextureBindings, 3, nullptr, 0);
    if (!computePass)
    {
        SDL_Log("Failed to begin compute pass: %s", SDL_GetError());
        return;
    }
    SDL_GPUTextureSamplerBinding textureBindings[4]{};
    textureBindings[0].sampler = sampler;
    textureBindings[0].texture = textures[TextureTypePressure].GetReadTexture();
    textureBindings[1].sampler = sampler;
    textureBindings[1].texture = textures[TextureTypeVelocityX].GetReadTexture();
    textureBindings[2].sampler = sampler;
    textureBindings[2].texture = textures[TextureTypeVelocityY].GetReadTexture();
    textureBindings[3].sampler = sampler;
    textureBindings[3].texture = textures[TextureTypeVelocityZ].GetReadTexture();
    int groups = (state.Size + THREADS - 1) / THREADS;
    SDL_BindGPUComputePipeline(computePass, pipelines[PipelineTypeProject3]);
    SDL_BindGPUComputeSamplers(computePass, 0, textureBindings, 4);
    SDL_DispatchGPUCompute(computePass, groups, groups, groups);
    SDL_EndGPUComputePass(computePass);
    textures[TextureTypeVelocityX].Swap();
    textures[TextureTypeVelocityY].Swap();
    textures[TextureTypeVelocityZ].Swap();
}

static void Advect1(SDL_GPUCommandBuffer* commandBuffer, TextureType texture)
{
    DebugGroup(commandBuffer);
    assert(texture == 0 || texture == 1 || texture == 2);
    SDL_GPUComputePass* computePass = textures[texture].BeginWritePass(commandBuffer);
    if (!computePass)
    {
        SDL_Log("Failed to begin compute pass: %s", SDL_GetError());
        return;
    }
    SDL_GPUTextureSamplerBinding textureBindings[3]{};
    textureBindings[0].sampler = sampler;
    textureBindings[0].texture = textures[TextureTypeVelocityX].GetReadTexture();
    textureBindings[1].sampler = sampler;
    textureBindings[1].texture = textures[TextureTypeVelocityY].GetReadTexture();
    textureBindings[2].sampler = sampler;
    textureBindings[2].texture = textures[TextureTypeVelocityZ].GetReadTexture();
    int groups = (state.Size + THREADS - 1) / THREADS;
    SDL_BindGPUComputePipeline(computePass, pipelines[PipelineTypeAdvect1]);
    SDL_BindGPUComputeSamplers(computePass, 0, textureBindings, 3);
    SDL_PushGPUComputeUniformData(commandBuffer, 0, &texture, sizeof(texture));
    SDL_PushGPUComputeUniformData(commandBuffer, 1, &speed, sizeof(speed));
    SDL_DispatchGPUCompute(computePass, groups, groups, groups);
    SDL_EndGPUComputePass(computePass);
}

static void Advect2(SDL_GPUCommandBuffer* commandBuffer)
{
    DebugGroup(commandBuffer);
    SDL_GPUComputePass* computePass = textures[TextureTypeDensity].BeginWritePass(commandBuffer);
    if (!computePass)
    {
        SDL_Log("Failed to begin compute pass: %s", SDL_GetError());
        return;
    }
    SDL_GPUTextureSamplerBinding textureBindings[4]{};
    textureBindings[0].sampler = sampler;
    textureBindings[0].texture = textures[TextureTypeDensity].GetReadTexture();
    textureBindings[1].sampler = sampler;
    textureBindings[1].texture = textures[TextureTypeVelocityX].GetReadTexture();
    textureBindings[2].sampler = sampler;
    textureBindings[2].texture = textures[TextureTypeVelocityY].GetReadTexture();
    textureBindings[3].sampler = sampler;
    textureBindings[3].texture = textures[TextureTypeVelocityZ].GetReadTexture();
    int groups = (state.Size + THREADS - 1) / THREADS;
    SDL_BindGPUComputePipeline(computePass, pipelines[PipelineTypeAdvect2]);
    SDL_BindGPUComputeSamplers(computePass, 0, textureBindings, 4);
    SDL_PushGPUComputeUniformData(commandBuffer, 0, &speed, sizeof(speed));
    SDL_DispatchGPUCompute(computePass, groups, groups, groups);
    SDL_EndGPUComputePass(computePass);
    textures[TextureTypeDensity].Swap();
}

static void Bnd1(SDL_GPUCommandBuffer* commandBuffer, ReadWriteTexture& texture, int type)
{
    DebugGroup(commandBuffer);
    SDL_GPUComputePass* computePass = texture.BeginWritePass(commandBuffer);
    if (!computePass)
    {
        SDL_Log("Failed to begin compute pass: %s", SDL_GetError());
        return;
    }
    SDL_GPUTextureSamplerBinding textureBindings{};
    textureBindings.sampler = sampler;
    textureBindings.texture = texture.GetReadTexture();
    int groups = (state.Size + THREADS - 1) / THREADS;
    SDL_BindGPUComputePipeline(computePass, pipelines[PipelineTypeBnd1]);
    SDL_BindGPUComputeSamplers(computePass, 0, &textureBindings, 1);
    SDL_PushGPUComputeUniformData(commandBuffer, 0, &type, sizeof(type));
    SDL_DispatchGPUCompute(computePass, groups, groups, 2);
    SDL_EndGPUComputePass(computePass);
}

static void Bnd2(SDL_GPUCommandBuffer* commandBuffer, ReadWriteTexture& texture, int type)
{
    DebugGroup(commandBuffer);
    SDL_GPUComputePass* computePass = texture.BeginWritePass(commandBuffer);
    if (!computePass)
    {
        SDL_Log("Failed to begin compute pass: %s", SDL_GetError());
        return;
    }
    SDL_GPUTextureSamplerBinding textureBindings{};
    textureBindings.sampler = sampler;
    textureBindings.texture = texture.GetReadTexture();
    int groups = (state.Size + THREADS - 1) / THREADS;
    SDL_BindGPUComputePipeline(computePass, pipelines[PipelineTypeBnd2]);
    SDL_BindGPUComputeSamplers(computePass, 0, &textureBindings, 1);
    SDL_PushGPUComputeUniformData(commandBuffer, 0, &type, sizeof(type));
    SDL_DispatchGPUCompute(computePass, groups, 2, groups);
    SDL_EndGPUComputePass(computePass);
}

static void Bnd3(SDL_GPUCommandBuffer* commandBuffer, ReadWriteTexture& texture, int type)
{
    DebugGroup(commandBuffer);
    SDL_GPUComputePass* computePass = texture.BeginWritePass(commandBuffer);
    if (!computePass)
    {
        SDL_Log("Failed to begin compute pass: %s", SDL_GetError());
        return;
    }
    SDL_GPUTextureSamplerBinding textureBindings{};
    textureBindings.sampler = sampler;
    textureBindings.texture = texture.GetReadTexture();
    int groups = (state.Size + THREADS - 1) / THREADS;
    SDL_BindGPUComputePipeline(computePass, pipelines[PipelineTypeBnd3]);
    SDL_BindGPUComputeSamplers(computePass, 0, &textureBindings, 1);
    SDL_PushGPUComputeUniformData(commandBuffer, 0, &type, sizeof(type));
    SDL_DispatchGPUCompute(computePass, 2, groups, groups);
    SDL_EndGPUComputePass(computePass);
}

static void Bnd4(SDL_GPUCommandBuffer* commandBuffer, ReadWriteTexture& texture)
{
    DebugGroup(commandBuffer);
    SDL_GPUComputePass* computePass = texture.BeginWritePass(commandBuffer);
    if (!computePass)
    {
        SDL_Log("Failed to begin compute pass: %s", SDL_GetError());
        return;
    }
    SDL_GPUTextureSamplerBinding textureBindings{};
    textureBindings.sampler = sampler;
    textureBindings.texture = texture.GetReadTexture();
    SDL_BindGPUComputePipeline(computePass, pipelines[PipelineTypeBnd4]);
    SDL_BindGPUComputeSamplers(computePass, 0, &textureBindings, 1);
    SDL_DispatchGPUCompute(computePass, 1, 1, 1);
    SDL_EndGPUComputePass(computePass);
}

static void Bnd5(SDL_GPUCommandBuffer* commandBuffer, ReadWriteTexture& texture)
{
    DebugGroup(commandBuffer);
    SDL_GPUComputePass* computePass = texture.BeginWritePass(commandBuffer);
    if (!computePass)
    {
        SDL_Log("Failed to begin compute pass: %s", SDL_GetError());
        return;
    }
    SDL_GPUTextureSamplerBinding textureBindings{};
    textureBindings.sampler = sampler;
    textureBindings.texture = texture.GetReadTexture();
    int groups = (state.Size + THREADS - 1) / THREADS;
    SDL_BindGPUComputePipeline(computePass, pipelines[PipelineTypeBnd5]);
    SDL_BindGPUComputeSamplers(computePass, 0, &textureBindings, 1);
    SDL_DispatchGPUCompute(computePass, groups, groups, groups);
    SDL_EndGPUComputePass(computePass);
}

static void Bnd(SDL_GPUCommandBuffer* commandBuffer, ReadWriteTexture& texture, int type)
{
    Bnd1(commandBuffer, texture, type);
    Bnd2(commandBuffer, texture, type);
    Bnd3(commandBuffer, texture, type);
    Bnd4(commandBuffer, texture);
    Bnd5(commandBuffer, texture);
    texture.Swap();
}

static void RenderComposite(SDL_GPUCommandBuffer* commandBuffer)
{
    DebugGroup(commandBuffer);
    SDL_GPUStorageTextureReadWriteBinding colorBinding{};
    colorBinding.texture = colorTexture;
    SDL_GPUComputePass* computePass = SDL_BeginGPUComputePass(commandBuffer, &colorBinding, 1, nullptr, 0);
    if (!computePass)
    {
        SDL_Log("Failed to begin compute pass: %s", SDL_GetError());
        return;
    }
    SDL_GPUTextureSamplerBinding textureBindings[4]{};
    textureBindings[0].sampler = sampler;
    textureBindings[0].texture = textures[TextureTypeVelocityX].GetReadTexture();
    textureBindings[1].sampler = sampler;
    textureBindings[1].texture = textures[TextureTypeVelocityY].GetReadTexture();
    textureBindings[2].sampler = sampler;
    textureBindings[2].texture = textures[TextureTypeVelocityZ].GetReadTexture();
    textureBindings[3].sampler = sampler;
    textureBindings[3].texture = textures[TextureTypeDensity].GetReadTexture();
    int groupsX = (kWidth + THREADS - 1) / THREADS;
    int groupsY = (kHeight + THREADS - 1) / THREADS;
    SDL_BindGPUComputePipeline(computePass, pipelines[PipelineTypeComposite]);
    SDL_BindGPUComputeSamplers(computePass, 0, textureBindings, 4);
    SDL_PushGPUComputeUniformData(commandBuffer, 0, &inverseView, sizeof(inverseView));
    SDL_PushGPUComputeUniformData(commandBuffer, 1, &inverseProj, sizeof(inverseProj));
    SDL_PushGPUComputeUniformData(commandBuffer, 2, &position, sizeof(position));
    SDL_DispatchGPUCompute(computePass, groupsX, groupsY, 1);
    SDL_EndGPUComputePass(computePass);
}

static void RenderSingle(SDL_GPUCommandBuffer* commandBuffer)
{
    DebugGroup(commandBuffer);
    SDL_GPUStorageTextureReadWriteBinding colorBinding{};
    colorBinding.texture = colorTexture;
    SDL_GPUComputePass* computePass = SDL_BeginGPUComputePass(commandBuffer, &colorBinding, 1, nullptr, 0);
    if (!computePass)
    {
        SDL_Log("Failed to begin compute pass: %s", SDL_GetError());
        return;
    }
    SDL_GPUTextureSamplerBinding textureBinding{};
    textureBinding.sampler = sampler;
    textureBinding.texture = textures[texture].GetReadTexture();
    int groupsX = (kWidth + THREADS - 1) / THREADS;
    int groupsY = (kHeight + THREADS - 1) / THREADS;
    SDL_BindGPUComputePipeline(computePass, pipelines[PipelineTypeSingle]);
    SDL_BindGPUComputeSamplers(computePass, 0, &textureBinding, 1);
    SDL_PushGPUComputeUniformData(commandBuffer, 0, &inverseView, sizeof(inverseView));
    SDL_PushGPUComputeUniformData(commandBuffer, 1, &inverseProj, sizeof(inverseProj));
    SDL_PushGPUComputeUniformData(commandBuffer, 2, &position, sizeof(position));
    SDL_DispatchGPUCompute(computePass, groupsX, groupsY, 1);
    SDL_EndGPUComputePass(computePass);
}

static void Letterbox(SDL_GPUCommandBuffer* commandBuffer, SDL_GPUTexture* swapchainTexture)
{
    DebugGroup(commandBuffer);
    SDL_GPUBlitInfo info{};
    const float colorRatio = static_cast<float>(kWidth) / kHeight;
    const float swapchainRatio = static_cast<float>(width) / height;
    float scale = 0.0f;
    float letterboxW = 0.0f;
    float letterboxH = 0.0f;
    float letterboxX = 0.0f;
    float letterboxY = 0.0f;
    if (colorRatio > swapchainRatio)
    {
        scale = static_cast<float>(width) / kWidth;
        letterboxW = width;
        letterboxH = kHeight * scale;
        letterboxX = 0.0f;
        letterboxY = (height - letterboxH) / 2.0f;
    }
    else
    {
        scale = static_cast<float>(height) / kHeight;
        letterboxH = height;
        letterboxW = kWidth * scale;
        letterboxX = (width - letterboxW) / 2.0f;
        letterboxY = 0.0f;
    }
    SDL_FColor clearColor = {0.02f, 0.02f, 0.02f, 1.0f};
    info.load_op = SDL_GPU_LOADOP_CLEAR;
    info.clear_color = clearColor;
    info.source.texture = colorTexture;
    info.source.w = kWidth;
    info.source.h = kHeight;
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
        // NOTE: not an error. can happen on minimize
        SDL_CancelGPUCommandBuffer(commandBuffer);
        return;
    }
    UpdateImGui(commandBuffer);
    UpdateViewProj();
    if (cooldown <= 0)
    {
        for (int i = 0; i < state.Iterations; i++)
        {
            Diffuse(commandBuffer, textures[TextureTypeVelocityX], state.Viscosity);
            Diffuse(commandBuffer, textures[TextureTypeVelocityY], state.Viscosity);
            Diffuse(commandBuffer, textures[TextureTypeVelocityZ], state.Viscosity);
            Bnd(commandBuffer, textures[TextureTypeVelocityX], 1);
            Bnd(commandBuffer, textures[TextureTypeVelocityY], 2);
            Bnd(commandBuffer, textures[TextureTypeVelocityZ], 3);
        }
        Project1(commandBuffer);
        Bnd(commandBuffer, textures[TextureTypeDivergence], 0);
        Bnd(commandBuffer, textures[TextureTypePressure], 0);
        for (int i = 0; i < state.Iterations; i++)
        {
            Project2(commandBuffer);
            Bnd(commandBuffer, textures[TextureTypePressure], 0);
        }
        Project3(commandBuffer);
        Bnd(commandBuffer, textures[TextureTypeVelocityX], 1);
        Bnd(commandBuffer, textures[TextureTypeVelocityY], 2);
        Bnd(commandBuffer, textures[TextureTypeVelocityZ], 3);
        Advect1(commandBuffer, TextureTypeVelocityX);
        Advect1(commandBuffer, TextureTypeVelocityY);
        Advect1(commandBuffer, TextureTypeVelocityZ);
        textures[TextureTypeVelocityX].Swap();
        textures[TextureTypeVelocityY].Swap();
        textures[TextureTypeVelocityZ].Swap();
        Bnd(commandBuffer, textures[TextureTypeVelocityX], 1);
        Bnd(commandBuffer, textures[TextureTypeVelocityY], 2);
        Bnd(commandBuffer, textures[TextureTypeVelocityZ], 3);
        Project1(commandBuffer);
        Project2(commandBuffer);
        Project3(commandBuffer);
        Diffuse(commandBuffer, textures[TextureTypeDensity], state.Diffusion);
        Advect2(commandBuffer);
        Bnd(commandBuffer, textures[TextureTypeDensity], 0);
        cooldown = kCooldown;
    }
    if (texture == TextureTypeCount)
    {
        RenderComposite(commandBuffer);
    }
    else
    {
        RenderSingle(commandBuffer);
    }
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
        float deltaTime = time2 - time1;
        cooldown -= deltaTime;
        time1 = time2;
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL3_ProcessEvent(&event);
            switch (event.type)
            {
            case SDL_EVENT_MOUSE_WHEEL:
                if (!hovered)
                {
                    distance = std::max(1.0f, distance - event.wheel.y * kZoom);
                }
                break;
            case SDL_EVENT_MOUSE_MOTION:
                if (!focused && !hovered && event.motion.state & (SDL_BUTTON_LMASK | SDL_BUTTON_RMASK))
                {
                    float limit = glm::pi<float>() / 2.0f - 0.01f;
                    yaw += event.motion.xrel * kPan;
                    pitch -= event.motion.yrel * kPan;
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
    for (int i = 0; i < TextureTypeCount; i++)
    {
        textures[i].Free(device);
    }
    SDL_ReleaseGPUTexture(device, colorTexture);
    SDL_ReleaseGPUSampler(device, sampler);
    for (int i = 0; i < PipelineTypeCount; i++)
    {
        SDL_ReleaseGPUComputePipeline(device, pipelines[i]);
        pipelines[i] = nullptr;
    }
    ImGui_ImplSDLGPU3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_ReleaseWindowFromGPUDevice(device, window);
    SDL_DestroyGPUDevice(device);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
