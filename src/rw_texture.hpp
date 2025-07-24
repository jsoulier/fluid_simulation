#pragma once

#include <SDL3/SDL.h>

class ReadWriteTexture
{
public:
    ReadWriteTexture() : textures{}, readIndex{} {}
    bool Create(SDL_GPUDevice* device, int size);
    void Free(SDL_GPUDevice* device);
    SDL_GPUComputePass* BeginReadPass(SDL_GPUCommandBuffer* commandBuffer);
    SDL_GPUComputePass* BeginWritePass(SDL_GPUCommandBuffer* commandBuffer);
    void Swap();
    SDL_GPUTexture* GetReadTexture();
    SDL_GPUTexture* GetWriteTexture();

private:
    SDL_GPUTexture* textures[2];
    int readIndex;
};