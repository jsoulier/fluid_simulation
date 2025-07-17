#include <SDL3/SDL.h>

#include "pipeline.hpp"
#include "shader.hpp"

static SDL_GPUGraphicsPipeline* graphicsPipelines[GraphicsPipelineTypeCount];
static SDL_GPUComputePipeline* computePipelines[ComputePipelineTypeCount];

bool CreatePipelines(SDL_GPUDevice* device, SDL_Window* window)
{
    SDL_GPUShader* allFragShader = LoadShader(device, "all.frag");
    SDL_GPUShader* allVertShader = LoadShader(device, "all.vert");
    SDL_GPUShader* debugFragShader = LoadShader(device, "debug.frag");
    SDL_GPUShader* debugVertShader = LoadShader(device, "debug.vert");
    SDL_GPUShader* outlineFragShader = LoadShader(device, "outline.frag");
    SDL_GPUShader* outlineVertShader = LoadShader(device, "outline.vert");
    if (!allFragShader || !allVertShader || !debugFragShader ||
        !debugVertShader || !outlineFragShader || !outlineVertShader)
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
    target.blend_state.enable_blend = true;
    target.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    target.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    target.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    target.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    target.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    target.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    SDL_GPUGraphicsPipelineCreateInfo info{};
    info.vertex_input_state.vertex_buffer_descriptions = &buffer;
    info.vertex_input_state.num_vertex_buffers = 1;
    info.vertex_input_state.vertex_attributes = &attrib;
    info.vertex_input_state.num_vertex_attributes = 1;
    info.target_info.color_target_descriptions = &target;
    info.target_info.num_color_targets = 1;
    info.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    info.target_info.has_depth_stencil_target = true;
    info.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;
    info.depth_stencil_state.enable_depth_write = true;
    info.vertex_shader = allVertShader;
    info.fragment_shader = allFragShader;
    graphicsPipelines[GraphicsPipelineTypeAll] = SDL_CreateGPUGraphicsPipeline(device, &info);
    info.vertex_shader = debugVertShader;
    info.fragment_shader = debugFragShader;
    graphicsPipelines[GraphicsPipelineTypeDebug] = SDL_CreateGPUGraphicsPipeline(device, &info);
    info.vertex_shader = outlineVertShader;
    info.fragment_shader = outlineFragShader;
    info.depth_stencil_state.enable_depth_test = true;
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
    computePipelines[ComputePipelineTypeAdd1] = LoadComputePipeline(device, "add1.comp");
    computePipelines[ComputePipelineTypeAdd2] = LoadComputePipeline(device, "add2.comp");
    computePipelines[ComputePipelineTypeClear] = LoadComputePipeline(device, "clear.comp");
    computePipelines[ComputePipelineTypeDiffuse] = LoadComputePipeline(device, "diffuse.comp");
    computePipelines[ComputePipelineTypeProject1] = LoadComputePipeline(device, "project1.comp");
    computePipelines[ComputePipelineTypeProject2] = LoadComputePipeline(device, "project2.comp");
    computePipelines[ComputePipelineTypeProject3] = LoadComputePipeline(device, "project3.comp");
    computePipelines[ComputePipelineTypeAdvect1] = LoadComputePipeline(device, "advect1.comp");
    computePipelines[ComputePipelineTypeAdvect2] = LoadComputePipeline(device, "advect2.comp");
    computePipelines[ComputePipelineTypeSetBnd1] = LoadComputePipeline(device, "set_bnd1.comp");
    computePipelines[ComputePipelineTypeSetBnd2] = LoadComputePipeline(device, "set_bnd2.comp");
    computePipelines[ComputePipelineTypeSetBnd3] = LoadComputePipeline(device, "set_bnd3.comp");
    computePipelines[ComputePipelineTypeSetBnd4] = LoadComputePipeline(device, "set_bnd4.comp");
    computePipelines[ComputePipelineTypeSetBnd5] = LoadComputePipeline(device, "set_bnd5.comp");
    for (int i = ComputePipelineTypeCount - 1; i >= 0; i--)
    {
        if (!computePipelines[i])
        {
            SDL_Log("Failed to create compute pipeline: %d", i);
            return false;
        }
    }
    SDL_ReleaseGPUShader(device, allFragShader);
    SDL_ReleaseGPUShader(device, allVertShader);
    SDL_ReleaseGPUShader(device, debugFragShader);
    SDL_ReleaseGPUShader(device, debugVertShader);
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