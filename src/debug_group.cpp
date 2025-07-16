#include <SDL3/SDL.h>

#include "debug_group.hpp"

DebugGroup::DebugGroup(SDL_GPUDevice* device, SDL_GPUCommandBuffer* commandBuffer, const char* name)
#ifndef NDEBUG
    : device{device}
    , commandBuffer{commandBuffer}
#endif
{
    /* TODO: waiting on https://github.com/libsdl-org/SDL/issues/12056 */
#ifndef NDEBUG
    if (!(SDL_GetGPUShaderFormats(device) & SDL_GPU_SHADERFORMAT_DXIL))
    {
        SDL_PushGPUDebugGroup(commandBuffer, name);
    }
#endif
}

DebugGroup::~DebugGroup()
{
#ifndef NDEBUG
    if (!(SDL_GetGPUShaderFormats(device) & SDL_GPU_SHADERFORMAT_DXIL))
    {
        SDL_PopGPUDebugGroup(commandBuffer);
    }
#endif
}