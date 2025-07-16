#pragma once

#include <SDL3/SDL.h>

#include <cstdint>

enum MeshType
{
    MeshTypeTriangleCube,
    MeshTypeLineCube,
};

bool CreateMeshes(SDL_GPUDevice* device);
void FreeMeshes(SDL_GPUDevice* device);
void RenderMesh(SDL_GPURenderPass* renderPass, MeshType type, uint32_t instances = 1);