#include <SDL3/SDL.h>

#include <cassert>
#include <cstdint>
#include <cstring>

#include "mesh.hpp"

static SDL_GPUBuffer* vertexBuffer;
static SDL_GPUBuffer* cubeIndexBuffer;
static SDL_GPUBuffer* lineIndexBuffer;

bool CreateMeshes(SDL_GPUDevice* device)
{
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
    float vertices[24] =
    {
       -0.5f, -0.5f,  0.5f,
        0.5f, -0.5f,  0.5f,
        0.5f,  0.5f,  0.5f,
       -0.5f,  0.5f,  0.5f,
       -0.5f, -0.5f, -0.5f,
        0.5f, -0.5f, -0.5f,
        0.5f,  0.5f, -0.5f,
       -0.5f,  0.5f, -0.5f,
    };
    uint32_t cubeIndices[36] = {
        0, 1, 2,
        0, 2, 3,
        5, 4, 7,
        5, 7, 6,
        4, 0, 3,
        4, 3, 7,
        1, 5, 6,
        1, 6, 2,
        3, 2, 6,
        3, 6, 7,
        4, 5, 1,
        4, 1, 0,
    };
    uint32_t lineIndices[24] =
    {
        0, 1,
        1, 2,
        2, 3,
        3, 0,
        4, 5,
        5, 6,
        6, 7,
        7, 4,
        0, 4,
        1, 5,
        2, 6,
        3, 7,
    };
    SDL_GPUTransferBuffer* voxelTransferBuffer;
    SDL_GPUTransferBuffer* cubeIndexTransferBuffer;
    SDL_GPUTransferBuffer* lineIndexTransferBuffer;
    {
        SDL_GPUTransferBufferCreateInfo info{};
        info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        info.size = sizeof(vertices);
        voxelTransferBuffer = SDL_CreateGPUTransferBuffer(device, &info);
        info.size = sizeof(cubeIndices);
        cubeIndexTransferBuffer = SDL_CreateGPUTransferBuffer(device, &info);
        info.size = sizeof(lineIndices);
        lineIndexTransferBuffer = SDL_CreateGPUTransferBuffer(device, &info);
        if (!voxelTransferBuffer || !cubeIndexTransferBuffer || !lineIndexTransferBuffer)
        {
            SDL_Log("Failed to create transfer buffer(s): %s", SDL_GetError());
            return false;
        }
    }
    void* voxelData = SDL_MapGPUTransferBuffer(device, voxelTransferBuffer, false);
    void* cubeIndexData = SDL_MapGPUTransferBuffer(device, cubeIndexTransferBuffer, false);
    void* lineIndexData = SDL_MapGPUTransferBuffer(device, lineIndexTransferBuffer, false);
    if (!voxelData || !cubeIndexData || !lineIndexData)
    {
        SDL_Log("Failed to map transfer buffer(s): %s", SDL_GetError());
        return false;
    }
    {
        SDL_GPUBufferCreateInfo info{};
        info.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
        info.size = sizeof(vertices);
        vertexBuffer = SDL_CreateGPUBuffer(device, &info);
        info.usage = SDL_GPU_BUFFERUSAGE_INDEX;
        info.size = sizeof(cubeIndices);
        cubeIndexBuffer = SDL_CreateGPUBuffer(device, &info);
        info.usage = SDL_GPU_BUFFERUSAGE_INDEX;
        info.size = sizeof(cubeIndices);
        lineIndexBuffer = SDL_CreateGPUBuffer(device, &info);
        if (!vertexBuffer || !cubeIndexBuffer || !lineIndexBuffer)
        {
            SDL_Log("Failed to create buffer(s): %s", SDL_GetError());
            return false;
        }
    }
    std::memcpy(voxelData, vertices, sizeof(vertices));
    std::memcpy(cubeIndexData, cubeIndices, sizeof(cubeIndices));
    std::memcpy(lineIndexData, lineIndices, sizeof(lineIndices));
    SDL_GPUTransferBufferLocation location{};
    SDL_GPUBufferRegion region{};
    location.transfer_buffer = voxelTransferBuffer;
    region.buffer = vertexBuffer;
    region.size = sizeof(vertices);
    SDL_UploadToGPUBuffer(copyPass, &location, &region, false);
    location.transfer_buffer = cubeIndexTransferBuffer;
    region.buffer = cubeIndexBuffer;
    region.size = sizeof(cubeIndices);
    SDL_UploadToGPUBuffer(copyPass, &location, &region, false);
    location.transfer_buffer = lineIndexTransferBuffer;
    region.buffer = lineIndexBuffer;
    region.size = sizeof(lineIndices);
    SDL_UploadToGPUBuffer(copyPass, &location, &region, false);
    SDL_ReleaseGPUTransferBuffer(device, voxelTransferBuffer);
    SDL_ReleaseGPUTransferBuffer(device, cubeIndexTransferBuffer);
    SDL_ReleaseGPUTransferBuffer(device, lineIndexTransferBuffer);
    SDL_EndGPUCopyPass(copyPass);
    SDL_SubmitGPUCommandBuffer(commandBuffer);
    return true;
}

void FreeMeshes(SDL_GPUDevice* device)
{
    SDL_ReleaseGPUBuffer(device, vertexBuffer);
    SDL_ReleaseGPUBuffer(device, cubeIndexBuffer);
    SDL_ReleaseGPUBuffer(device, lineIndexBuffer);
    vertexBuffer = nullptr;
    cubeIndexBuffer = nullptr;
    lineIndexBuffer = nullptr;
}

void RenderMesh(SDL_GPURenderPass* renderPass, MeshType type, uint32_t instances)
{
    SDL_GPUBufferBinding vertexBufferBinding{};
    SDL_GPUBufferBinding indexBufferBinding{};
    uint32_t indices;
    switch (type)
    {
    case MeshTypeTriangleCube:
        vertexBufferBinding.buffer = vertexBuffer;
        indexBufferBinding.buffer = cubeIndexBuffer;
        indices = 36;
        break;
    case MeshTypeLineCube:
        vertexBufferBinding.buffer = vertexBuffer;
        indexBufferBinding.buffer = lineIndexBuffer;
        indices = 24;
        break;
    default:
        assert(false);
    }
    SDL_BindGPUVertexBuffers(renderPass, 0, &vertexBufferBinding, 1);
    SDL_BindGPUIndexBuffer(renderPass, &indexBufferBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
    SDL_DrawGPUIndexedPrimitives(renderPass, indices, instances, 0, 0, 0);
}