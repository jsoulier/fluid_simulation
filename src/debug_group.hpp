#pragma once

#include <SDL3/SDL.h>

#define DEBUG_GROUP(device, commandBuffer) \
    DebugGroup debugGroup##__func__(device, commandBuffer, __func__)

struct DebugGroup
{
    DebugGroup(SDL_GPUDevice* device, SDL_GPUCommandBuffer* commandBuffer, const char* name);
    ~DebugGroup();

#ifndef NDEBUG
    SDL_GPUDevice* device;
    SDL_GPUCommandBuffer* commandBuffer;
#endif
};