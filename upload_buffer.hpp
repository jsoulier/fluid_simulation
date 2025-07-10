#pragma once

#include <SDL3/SDL.h>

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <utility>

template<typename T, SDL_GPUBufferUsageFlags U>
class UploadBuffer;
template<typename T>
using VertexUploadBuffer = UploadBuffer<T, SDL_GPU_BUFFERUSAGE_VERTEX>;
template<typename T>
using IndexUploadBuffer = UploadBuffer<T, SDL_GPU_BUFFERUSAGE_INDEX>;
template<typename T>
using ComputeUploadBuffer = UploadBuffer<T, SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ>;

template<typename T, SDL_GPUBufferUsageFlags U>
class UploadBuffer
{
public:
    UploadBuffer()
        : buffer{nullptr}
        , transferBuffer{nullptr}
        , bufferSize{0}
        , transferBufferSize{0}
        , bufferCapacity{0}
        , transferBufferCapacity{0}
        , data{nullptr} {}

    void Destroy(SDL_GPUDevice* device)
    {
        SDL_ReleaseGPUBuffer(device, buffer);
        buffer = nullptr;
        bufferSize = 0;
        bufferCapacity = 0;
        DestroyTransferBuffer(device);
    }

    void DestroyTransferBuffer(SDL_GPUDevice* device)
    {
        SDL_ReleaseGPUTransferBuffer(device, transferBuffer);
        transferBuffer = nullptr;
        transferBufferSize = 0;
        transferBufferCapacity = 0;
        data = nullptr;
    }

    template<typename... Args>
    void Emplace(SDL_GPUDevice* device, Args&&... args)
    {
        if (!data && transferBuffer)
        {
            bufferSize = 0;
            assert(!transferBufferSize);
            data = static_cast<T*>(SDL_MapGPUTransferBuffer(device, transferBuffer, true));
            if (!data)
            {
                SDL_Log("Failed to map transfer buffer: %s", SDL_GetError());
                return;
            }
        }
        if (transferBufferSize == transferBufferCapacity)
        {
            static constexpr uint32_t StartingCapacity = 10;
            static constexpr uint32_t GrowthRate = 2;
            uint32_t capacity = std::max(StartingCapacity, transferBufferSize * GrowthRate);
            SDL_GPUTransferBufferCreateInfo info{};
            info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
            info.size = capacity * sizeof(T);
            SDL_GPUTransferBuffer* transferBuffer = SDL_CreateGPUTransferBuffer(device, &info);
            if (!transferBuffer)
            {
                SDL_Log("Failed to create transfer buffer: %s", SDL_GetError());
                return;
            }
            T* data = static_cast<T*>(SDL_MapGPUTransferBuffer(device, transferBuffer, false));
            if (!data)
            {
                SDL_Log("Failed to map transfer buffer: %s", SDL_GetError());
                SDL_ReleaseGPUTransferBuffer(device, transferBuffer);
                return;
            }
            if (this->data)
            {
                std::memcpy(data, this->data, transferBufferSize * sizeof(T));
                SDL_UnmapGPUTransferBuffer(device, this->transferBuffer);
            }
            SDL_ReleaseGPUTransferBuffer(device, this->transferBuffer);
            transferBufferCapacity = capacity;
            this->transferBuffer = transferBuffer;
            this->data = data;
        }
        assert(data);
        data[transferBufferSize++] = T{std::forward<Args>(args)...};
    }

    void Upload(SDL_GPUDevice* device, SDL_GPUCopyPass* copyPass)
    {
        if (data)
        {
            SDL_UnmapGPUTransferBuffer(device, transferBuffer);
            data = nullptr;
        }
        uint32_t size = transferBufferSize;
        transferBufferSize = 0;
        if (!size)
        {
            bufferSize = 0;
            return;
        }
        if (transferBufferCapacity > bufferCapacity)
        {
            SDL_ReleaseGPUBuffer(device, buffer);
            buffer = nullptr;
            bufferCapacity = 0;
            SDL_GPUBufferCreateInfo info{};
            info.usage = U;
            info.size = transferBufferCapacity * sizeof(T);
            buffer = SDL_CreateGPUBuffer(device, &info);
            if (!buffer)
            {
                SDL_Log("Failed to create buffer: %s", SDL_GetError());
                return;
            }
            bufferCapacity = transferBufferCapacity;
        }
        SDL_GPUTransferBufferLocation location{};
        SDL_GPUBufferRegion region{};
        location.transfer_buffer = transferBuffer;
        region.buffer = buffer;
        region.size = size * sizeof(T);
        SDL_UploadToGPUBuffer(copyPass, &location, &region, true);
        bufferSize = size;
    }

    void Upload(SDL_GPUDevice* device, SDL_GPUCommandBuffer* commandBuffer)
    {
        SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(commandBuffer);
        if (!copyPass)
        {
            SDL_Log("Failed to begin copy pass: %s", SDL_GetError());
            return;
        }
        Upload(device, copyPass);
        SDL_EndGPUCopyPass(copyPass);
    }

    void Upload(SDL_GPUDevice* device)
    {
        SDL_GPUCommandBuffer* commandBuffer = SDL_AcquireGPUCommandBuffer(device);
        if (!commandBuffer)
        {
            SDL_Log("Failed to acquire command buffer: %s", SDL_GetError());
            return;
        }
        Upload(device, commandBuffer);
        SDL_SubmitGPUCommandBuffer(commandBuffer);
    }

    SDL_GPUBuffer* GetBuffer() const
    {
        return buffer;
    }

    uint32_t GetBufferSize() const
    {
        return bufferSize;
    }

    uint32_t GetTransferBufferSize() const
    {
        return transferBufferSize;
    }

private:
    SDL_GPUBuffer* buffer;
    SDL_GPUTransferBuffer* transferBuffer;
    uint32_t bufferSize;
    uint32_t transferBufferSize;
    uint32_t bufferCapacity;
    uint32_t transferBufferCapacity;
    T* data;
};