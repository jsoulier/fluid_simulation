#include <SDL3/SDL.h>

#include "pipeline.hpp"
#include "shader.hpp"

static SDL_GPUGraphicsPipeline* graphicsPipelines[GraphicsPipelineTypeCount];
static SDL_GPUComputePipeline* computePipelines[ComputePipelineTypeCount];

bool CreatePipelines(SDL_GPUDevice* device, SDL_Window* window)
{
    SDL_GPUShader* voxelFragShader = LoadShader(device, "voxel.frag");
    SDL_GPUShader* voxelVertShader = LoadShader(device, "voxel.vert");
    SDL_GPUShader* outlineFragShader = LoadShader(device, "outline.frag");
    SDL_GPUShader* outlineVertShader = LoadShader(device, "outline.vert");
    if (!voxelFragShader || !voxelVertShader || !outlineFragShader || !outlineVertShader)
    {
        SDL_Log("Failed to create shader(s)");
        return false;
    }
    SDL_GPUVertexBufferDescription buffer{};
    SDL_GPUVertexAttribute attrib{};
    SDL_GPUColorTargetDescription target{};
    buffer.slot = 0;
    buffer.pitch = sizeof(float) * 3;
    buffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    buffer.instance_step_rate = 0;
    attrib.location = 0;
    attrib.buffer_slot = 0;
    attrib.format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    attrib.offset = 0;
    target.format = SDL_GetGPUSwapchainTextureFormat(device, window);
    SDL_GPUGraphicsPipelineCreateInfo info{};
    info.vertex_shader = voxelVertShader;
    info.fragment_shader = voxelFragShader;
    info.vertex_input_state.vertex_buffer_descriptions = &buffer;
    info.vertex_input_state.num_vertex_buffers = 1;
    info.vertex_input_state.vertex_attributes = &attrib;
    info.vertex_input_state.num_vertex_attributes = 1;
    info.target_info.color_target_descriptions = &target;
    info.target_info.num_color_targets = 1;
    info.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    info.target_info.has_depth_stencil_target = true;
    info.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;
    info.depth_stencil_state.enable_depth_test = true;
    info.depth_stencil_state.enable_depth_write = true;
    graphicsPipelines[GraphicsPipelineTypeVoxel] = SDL_CreateGPUGraphicsPipeline(device, &info);
    info.vertex_shader = outlineVertShader;
    info.fragment_shader = outlineFragShader;
    info.primitive_type = SDL_GPU_PRIMITIVETYPE_LINELIST;
    graphicsPipelines[GraphicsPipelineTypeOutline] = SDL_CreateGPUGraphicsPipeline(device, &info);
    for (int i = GraphicsPipelineTypeCount - 1; i >= 0; i--)
    {
        if (!graphicsPipelines[i])
        {
            SDL_Log("Failed to create graphics pipeline: %d, %s", i, SDL_GetError());
            return false;
        }
    }
    computePipelines[ComputePipelineTypeAdd] = LoadComputePipeline(device, "add.comp");
    computePipelines[ComputePipelineTypeClear] = LoadComputePipeline(device, "clear.comp");
    computePipelines[ComputePipelineTypeDiffuse] = LoadComputePipeline(device, "diffuse.comp");
    computePipelines[ComputePipelineTypeProject1] = LoadComputePipeline(device, "project1.comp");
    computePipelines[ComputePipelineTypeProject2] = LoadComputePipeline(device, "project2.comp");
    computePipelines[ComputePipelineTypeProject3] = LoadComputePipeline(device, "project3.comp");
    computePipelines[ComputePipelineTypeAdvect] = LoadComputePipeline(device, "advect.comp");
    for (int i = ComputePipelineTypeCount - 1; i >= 0; i--)
    {
        if (!computePipelines[i])
        {
            SDL_Log("Failed to create compute pipeline: %d", i);
            return false;
        }
    }
    SDL_ReleaseGPUShader(device, voxelFragShader);
    SDL_ReleaseGPUShader(device, voxelVertShader);
    SDL_ReleaseGPUShader(device, outlineFragShader);
    SDL_ReleaseGPUShader(device, outlineVertShader);
    return true;
}

void FreePipelines(SDL_GPUDevice* device)
{
    for (int i = 0; i < GraphicsPipelineTypeCount; i++)
    {
        SDL_ReleaseGPUGraphicsPipeline(device, graphicsPipelines[i]);
        graphicsPipelines[i] = nullptr;
    }
    for (int i = 0; i < ComputePipelineTypeCount; i++)
    {
        SDL_ReleaseGPUComputePipeline(device, computePipelines[i]);
        computePipelines[i] = nullptr;
    }
}

void BindPipeline(SDL_GPURenderPass* renderPass, GraphicsPipelineType type)
{
    SDL_BindGPUGraphicsPipeline(renderPass, graphicsPipelines[type]);
}

void BindPipeline(SDL_GPUComputePass* computePass, ComputePipelineType type)
{
    SDL_BindGPUComputePipeline(computePass, computePipelines[type]);
}