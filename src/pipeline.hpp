#pragma once

#include <SDL3/SDL.h>

enum GraphicsPipelineType
{
    GraphicsPipelineTypeCombined,
    GraphicsPipelineTypeDebug,
    GraphicsPipelineTypeCount,
};

enum ComputePipelineType
{
    ComputePipelineTypeAdd1,
    ComputePipelineTypeAdd2,
    ComputePipelineTypeClear,
    ComputePipelineTypeDiffuse,
    ComputePipelineTypeProject1,
    ComputePipelineTypeProject2,
    ComputePipelineTypeProject3,
    ComputePipelineTypeAdvect1,
    ComputePipelineTypeAdvect2,
    ComputePipelineTypeSetBnd1,
    ComputePipelineTypeSetBnd2,
    ComputePipelineTypeSetBnd3,
    ComputePipelineTypeSetBnd4,
    ComputePipelineTypeSetBnd5,
    ComputePipelineTypeCount,
};

bool CreatePipelines(SDL_GPUDevice* device, SDL_Window* window);
void FreePipelines(SDL_GPUDevice* device);
void BindPipeline(SDL_GPURenderPass* renderPass, GraphicsPipelineType type);
void BindPipeline(SDL_GPUComputePass* computePass, ComputePipelineType type);