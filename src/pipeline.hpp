#pragma once

#include <SDL3/SDL.h>

enum GraphicsPipelineType
{
    GraphicsPipelineTypeVoxel,
    GraphicsPipelineTypeOutline,
    GraphicsPipelineTypeCount,
};

enum ComputePipelineType
{
    /* add to a single cell of an image */
    ComputePipelineTypeAdd1,
    /* add to all cells of an image */
    ComputePipelineTypeAdd2,
    /* clear an image to a value */
    ComputePipelineTypeClear,
    /* diffusion using linear solve */
    ComputePipelineTypeDiffuse,
    /* step 1 of projection for calculating divergence */
    ComputePipelineTypeProject1,
    /* step 2 of projection to get pressure using linear solve on divergence  */
    ComputePipelineTypeProject2,
    /* step 3 of projection for applying pressure */
    ComputePipelineTypeProject3,
    /* step 1 of advection for calculating velocity change */
    ComputePipelineTypeAdvect1,
    /* step 2 of advection for calculating density change */
    ComputePipelineTypeAdvect2,
    /* fix z boundaries */
    ComputePipelineTypeSetBnd1,
    /* fix y boundaries */
    ComputePipelineTypeSetBnd2,
    /* fix z boundaries */
    ComputePipelineTypeSetBnd3,
    /* fix corners */
    ComputePipelineTypeSetBnd4,
    /* copy non-border cells */
    ComputePipelineTypeSetBnd5,
    /* */
    ComputePipelineTypeCount,
};

bool CreatePipelines(SDL_GPUDevice* device, SDL_Window* window);
void FreePipelines(SDL_GPUDevice* device);
void BindPipeline(SDL_GPURenderPass* renderPass, GraphicsPipelineType type);
void BindPipeline(SDL_GPUComputePass* computePass, ComputePipelineType type);