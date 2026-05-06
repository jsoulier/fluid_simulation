#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "config.h"
#include "shader.hpp"

static constexpr float kPan = 0.002f;
static constexpr float kZoom = 25.0e9f;
static constexpr float kFov = glm::radians<float>(60.0f);
static constexpr float kC = 299792458.0f;
static constexpr float kG = 6.67430e-11f;
static constexpr float kBlackHoleMass = 8.54e36f;
static constexpr float kBlackHoleRadius = 2.0f * kG * kBlackHoleMass / (kC * kC);

struct UniformBuffer
{
    glm::vec3 CameraPosition;
    float TanHalfFov;
    glm::vec3 CameraRight;
    float Aspect;
    glm::vec3 CameraUp;
    uint32_t ObjectCount;
    glm::vec3 CameraForward;
    float DiskR1 = kBlackHoleRadius * 2.2f;
    float DiskR2 = kBlackHoleRadius * 5.2f;
    uint32_t FrameCount;
    uint32_t ResetAccumulation;
};

struct Object
{
    glm::vec3 Position;
    float Radius;
    glm::vec3 Color;
    float Mass;
};

static SDL_Window* window;
static SDL_GPUDevice* device;
static SDL_GPUComputePipeline* geodesicPipeline;
static SDL_GPUTexture* colorTexture;
static SDL_GPUTexture* accumulationTexture;
static SDL_GPUBuffer* objectBuffer;
static float pitch;
static float yaw;
static float distance{1.0e11f};
static UniformBuffer uniformBuffer;

static bool Init()
{
    SDL_SetAppMetadata("Black Hole Simulation", nullptr, nullptr);
    SDL_SetLogPriorities(SDL_LOG_PRIORITY_VERBOSE);
    
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        SDL_Log("Failed to initialize SDL: %s", SDL_GetError());
        return false;
    }
    window = SDL_CreateWindow("Black Hole Simulation", 960, 720, SDL_WINDOW_RESIZABLE);
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
    geodesicPipeline = LoadComputePipeline(device, "geodesic.comp");
    if (!geodesicPipeline)
    {
        SDL_Log("Failed to create pipeline");
        return false;
    }
    {
        SDL_GPUTextureCreateInfo info{};
        info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        info.usage = SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE | SDL_GPU_TEXTUREUSAGE_SAMPLER;
        info.type = SDL_GPU_TEXTURETYPE_2D;
        info.width = WIDTH;
        info.height = HEIGHT;
        info.layer_count_or_depth = 1;
        info.num_levels = 1;
        colorTexture = SDL_CreateGPUTexture(device, &info);
        if (!colorTexture)
        {
            SDL_Log("Failed to create color texture: %s", SDL_GetError());
            return false;
        }
        info.format = SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
        info.usage = SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE | SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_READ;
        accumulationTexture = SDL_CreateGPUTexture(device, &info);
        if (!accumulationTexture)
        {
            SDL_Log("Failed to create accumulation texture: %s", SDL_GetError());
            return false;
        }
    }
    SDL_GPUCommandBuffer* commandBuffer = SDL_AcquireGPUCommandBuffer(device);
    if (!commandBuffer)
    {
        SDL_Log("Failed to acquire command buffer: %s", SDL_GetError());
        return false;
    }
    SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(commandBuffer);
    if (!copyPass)
    {
        SDL_Log("Failed to begin copy pass: %s", SDL_GetError());
        return false;
    }
    uniformBuffer.ObjectCount = 3;
    SDL_GPUTransferBuffer* transferBuffer;
    {
        SDL_GPUTransferBufferCreateInfo info{};
        info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        info.size = uniformBuffer.ObjectCount * sizeof(Object);
        transferBuffer = SDL_CreateGPUTransferBuffer(device, &info);
        if (!transferBuffer)
        {
            SDL_Log("Failed to create transfer buffer: %s", SDL_GetError());
            return false;
        }
    }
    Object* objects = static_cast<Object*>(SDL_MapGPUTransferBuffer(device, transferBuffer, false));
    if (!objects)
    {
        SDL_Log("Failed to map transfer buffer: %s", SDL_GetError());
        return false;
    }
    objects[0] = {{4e11f, 0.0f, 0.0f}, 4e10f, {1, 1, 0}, 1.98892e30f};
    objects[1] = {{0.0f, 0.0f, 4e11f}, 4e10f, {1, 0, 0}, 1.98892e30f};
    objects[2] = {{0.0f, 0.0f, 0.0f}, kBlackHoleRadius, {0, 0, 0}, kBlackHoleMass};
    SDL_UnmapGPUTransferBuffer(device, transferBuffer);
    {
        SDL_GPUBufferCreateInfo info{};
        info.usage = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ;
        info.size = uniformBuffer.ObjectCount * sizeof(Object);
        objectBuffer = SDL_CreateGPUBuffer(device, &info);
        if (!objectBuffer)
        {
            SDL_Log("Failed to create buffer: %s", SDL_GetError());
            return false;
        }
    }
    {
        SDL_GPUTransferBufferLocation location{};
        SDL_GPUBufferRegion region{};
        location.transfer_buffer = transferBuffer;
        region.buffer = objectBuffer;
        region.size = uniformBuffer.ObjectCount * sizeof(Object);
        SDL_UploadToGPUBuffer(copyPass, &location, &region, false);
    }
    SDL_ReleaseGPUTransferBuffer(device, transferBuffer);
    SDL_EndGPUCopyPass(copyPass);
    SDL_SubmitGPUCommandBuffer(commandBuffer);
    return true;
}

static void Draw()
{
    SDL_GPUCommandBuffer* commandBuffer = SDL_AcquireGPUCommandBuffer(device);
    if (!commandBuffer)
    {
        SDL_Log("Failed to acquire command buffer: %s", SDL_GetError());
        return;
    }
    SDL_GPUTexture* swapchainTexture;
    uint32_t width;
    uint32_t height;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(commandBuffer, window, &swapchainTexture, &width, &height))
    {
        SDL_Log("Failed to acquire swapchain texture: %s", SDL_GetError());
        SDL_CancelGPUCommandBuffer(commandBuffer);
        return;
    }
    if (!swapchainTexture || !width || !height)
    {
        /* NOTE: not an error */
        SDL_SubmitGPUCommandBuffer(commandBuffer);
        return;
    }
    {
        uniformBuffer.TanHalfFov = std::tan(kFov * 0.5f);
        uniformBuffer.Aspect = float(WIDTH) / HEIGHT;
        uniformBuffer.CameraForward.x = std::cos(pitch) * std::cos(yaw);
        uniformBuffer.CameraForward.y = std::sin(pitch);
        uniformBuffer.CameraForward.z = std::cos(pitch) * std::sin(yaw);
        uniformBuffer.CameraForward = glm::normalize(uniformBuffer.CameraForward);
        uniformBuffer.CameraPosition = -uniformBuffer.CameraForward * distance;
        uniformBuffer.CameraRight = glm::cross(uniformBuffer.CameraForward, glm::vec3(0.0f, 1.0f, 0.0f));
        uniformBuffer.CameraRight = glm::normalize(uniformBuffer.CameraRight);
        uniformBuffer.CameraUp = glm::cross(uniformBuffer.CameraRight, uniformBuffer.CameraForward);
        uniformBuffer.CameraUp = glm::normalize(uniformBuffer.CameraUp);
        uniformBuffer.FrameCount++;
    }
    {
        SDL_GPUStorageTextureReadWriteBinding readWriteTextures[2]{};
        readWriteTextures[0].texture = colorTexture;
        readWriteTextures[1].texture = accumulationTexture;
        SDL_GPUComputePass* computePass = SDL_BeginGPUComputePass(commandBuffer, readWriteTextures, 2, nullptr, 0);
        if (!computePass)
        {
            SDL_Log("Failed to begin compute pass: %s", SDL_GetError());
            SDL_SubmitGPUCommandBuffer(commandBuffer);
            return;
        }
        int groupsX = (WIDTH + THREADS - 1) / THREADS;
        int groupsY = (HEIGHT + THREADS - 1) / THREADS;
        SDL_BindGPUComputePipeline(computePass, geodesicPipeline);
        SDL_PushGPUComputeUniformData(commandBuffer, 0, &uniformBuffer, sizeof(uniformBuffer));
        SDL_BindGPUComputeStorageBuffers(computePass, 0, &objectBuffer, 1);
        SDL_DispatchGPUCompute(computePass, groupsX, groupsY, 1);
        SDL_EndGPUComputePass(computePass);
        uniformBuffer.ResetAccumulation = 0;
    }
    {
        uint32_t letterboxW;
        uint32_t letterboxH;
        uint32_t letterboxX;
        uint32_t letterboxY;
        if ((float(WIDTH) / HEIGHT) > (float(width) / height))
        {
            letterboxW = width;
            letterboxH = HEIGHT * float(width) / WIDTH;
            letterboxX = 0;
            letterboxY = (height - letterboxH) / 2;
        }
        else
        {
            letterboxH = height;
            letterboxW = WIDTH * float(height) / HEIGHT;
            letterboxX = (width - letterboxW) / 2;
            letterboxY = 0;
        }
        SDL_FColor clearColor = {0.0f, 0.0f, 0.0f, 1.0f};
        SDL_GPUBlitInfo info{};
        info.load_op = SDL_GPU_LOADOP_CLEAR;
        info.clear_color = clearColor;
        info.source.texture = colorTexture;
        info.source.w = WIDTH;
        info.source.h = HEIGHT;
        info.destination.texture = swapchainTexture;
        info.destination.x = (float)letterboxX;
        info.destination.y = (float)letterboxY;
        info.destination.w = (float)letterboxW;
        info.destination.h = (float)letterboxH;
        info.filter = SDL_GPU_FILTER_LINEAR;
        SDL_BlitGPUTexture(commandBuffer, &info);
    }
    SDL_SubmitGPUCommandBuffer(commandBuffer);
}

int main(int argc, char** argv)
{
    if (!Init())
    {
        return 1;
    }
    bool running = true;
    while (running)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            switch (event.type)
            {
            case SDL_EVENT_MOUSE_WHEEL:
                distance = std::max(1.0f, distance - event.wheel.y * kZoom);
                uniformBuffer.ResetAccumulation = 1;
                break;
            case SDL_EVENT_MOUSE_MOTION:
                if (event.motion.state & SDL_BUTTON_LMASK)
                {
                    static constexpr float kClamp = glm::pi<float>() / 2.0f - 0.01f;
                    yaw += event.motion.xrel * kPan;
                    pitch = std::clamp(pitch + event.motion.yrel * kPan, -kClamp, kClamp);
                    uniformBuffer.ResetAccumulation = 1;
                }
                break;
            case SDL_EVENT_QUIT:
                running = false;
                break;
            }
        }
        Draw();
    }
    SDL_HideWindow(window);
    SDL_ReleaseGPUBuffer(device, objectBuffer);
    SDL_ReleaseGPUTexture(device, colorTexture);
    SDL_ReleaseGPUTexture(device, accumulationTexture);
    SDL_ReleaseGPUComputePipeline(device, geodesicPipeline);
    SDL_ReleaseWindowFromGPUDevice(device, window);
    SDL_DestroyGPUDevice(device);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
