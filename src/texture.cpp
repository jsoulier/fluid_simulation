#include <SDL3/SDL.h>

#include "texture.hpp"

bool ReadWriteTexture::Create(SDL_GPUDevice* device, int size)
{
    Free(device);
    SDL_GPUTextureCreateInfo info{};
    info.format = SDL_GPU_TEXTUREFORMAT_R32_FLOAT;
    info.type = SDL_GPU_TEXTURETYPE_3D;
    info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_SIMULTANEOUS_READ_WRITE;
    info.width = size;
    info.height = size;
    info.layer_count_or_depth = size;
    info.num_levels = 1;
    for (int i = 0; i < 2; i++)
    {
        Textures[i] = SDL_CreateGPUTexture(device, &info);
        if (!Textures[i])
        {
            SDL_Log("Failed to create texture: %s", SDL_GetError());
            return false;
        }
    }
    return true;
}

void ReadWriteTexture::Free(SDL_GPUDevice* device)
{
    SDL_ReleaseGPUTexture(device, Textures[0]);
    SDL_ReleaseGPUTexture(device, Textures[1]);
    Textures[0] = nullptr;
    Textures[1] = nullptr;
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
    ReadIndex = (ReadIndex + 1) % 2;
}

SDL_GPUTexture* ReadWriteTexture::GetReadTexture()
{
    return Textures[ReadIndex];
}

SDL_GPUTexture* ReadWriteTexture::GetWriteTexture()
{
    return Textures[(ReadIndex + 1) % 2];
}