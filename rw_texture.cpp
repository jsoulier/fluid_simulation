#include <SDL3/SDL.h>

#include "rw_texture.hpp"

bool ReadWriteTexture::Create(SDL_GPUDevice* device, int size)
{
    Free(device);
    SDL_GPUTextureCreateInfo info{};
    info.format = SDL_GPU_TEXTUREFORMAT_R32_FLOAT;
    info.type = SDL_GPU_TEXTURETYPE_3D;
    info.usage = SDL_GPU_TEXTUREUSAGE_GRAPHICS_STORAGE_READ |
        SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_SIMULTANEOUS_READ_WRITE;
    info.width = size;
    info.height = size;
    info.layer_count_or_depth = size;
    info.num_levels = 1;
    for (int i = 0; i < 2; i++)
    {
        textures[i] = SDL_CreateGPUTexture(device, &info);
        if (!textures[i])
        {
            SDL_Log("Failed to create texture: %s", SDL_GetError());
            return false;
        }
    }
    return true;
}

void ReadWriteTexture::Free(SDL_GPUDevice* device)
{
    for (int i = 0; i < 2; i++)
    {
        SDL_ReleaseGPUTexture(device, textures[i]);
        textures[i] = nullptr;
    }
}

SDL_GPUComputePass* ReadWriteTexture::BeginReadPass(SDL_GPUCommandBuffer* commandBuffer)
{
    SDL_GPUStorageTextureReadWriteBinding binding{};
    binding.texture = GetReadTexture();
    binding.cycle = false;
    return SDL_BeginGPUComputePass(commandBuffer, &binding, 1, nullptr, 0);
}

SDL_GPUComputePass* ReadWriteTexture::BeginWritePass(SDL_GPUCommandBuffer* commandBuffer)
{
    SDL_GPUStorageTextureReadWriteBinding binding{};
    binding.texture = GetWriteTexture();
    binding.cycle = false;
    return SDL_BeginGPUComputePass(commandBuffer, &binding, 1, nullptr, 0);
}

void ReadWriteTexture::Swap()
{
    readIndex = (readIndex + 1) % 2;
}

SDL_GPUTexture* ReadWriteTexture::GetReadTexture()
{
    return textures[readIndex];
}

SDL_GPUTexture* ReadWriteTexture::GetWriteTexture()
{
    return textures[(readIndex + 1) % 2];
}

SDL_GPUTexture** ReadWriteTexture::GetReadTextureAddress()
{
    return &textures[readIndex];
}

SDL_GPUTexture** ReadWriteTexture::GetWriteTextureAddress()
{
    return &textures[(readIndex + 1) % 2];
}