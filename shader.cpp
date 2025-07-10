#include <SDL3/SDL.h>

#include <cassert>
#include <cstdint>
#include <cstring>
#include <format>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

#include "jsmn.h"
#include "shader.hpp"

static void* Load(SDL_GPUDevice* device, const std::string_view& name)
{
    SDL_GPUShaderFormat shaderFormat = SDL_GetGPUShaderFormats(device);
    const char* entrypoint;
    const char* fileExtension;
    if (shaderFormat & SDL_GPU_SHADERFORMAT_SPIRV)
    {
        shaderFormat = SDL_GPU_SHADERFORMAT_SPIRV;
        entrypoint = "main";
        fileExtension = "spv";
    }
    else if (shaderFormat & SDL_GPU_SHADERFORMAT_DXIL)
    {
        shaderFormat = SDL_GPU_SHADERFORMAT_DXIL;
        entrypoint = "main";
        fileExtension = "dxil";
    }
    else if (shaderFormat & SDL_GPU_SHADERFORMAT_MSL)
    {
        shaderFormat = SDL_GPU_SHADERFORMAT_MSL;
        entrypoint = "main0";
        fileExtension = "msl";
    }
    else
    {
        assert(false);
    }
    std::string shaderPath = std::format("{}.{}", name, fileExtension);
    std::ifstream shaderFile(shaderPath, std::ios::binary);
    if (shaderFile.fail())
    {
        SDL_Log("Failed to open shader: %s", shaderPath.data());
        return nullptr;
    }
    std::string jsonPath = std::format("{}.json", name);
    std::ifstream jsonFile(jsonPath, std::ios::binary);
    if (jsonFile.fail())
    {
        SDL_Log("Failed to open json: %s", jsonPath.data());
        return nullptr;
    }
    std::string shaderData(std::istreambuf_iterator<char>(shaderFile), {});
    std::string jsonData(std::istreambuf_iterator<char>(jsonFile), {});
    jsmn_parser parser;
    jsmntok_t tokens[19];
    jsmn_init(&parser);
    if (jsmn_parse(&parser, jsonData.data(), jsonData.size(), tokens, 19) <= 0)
    {
        SDL_Log("Failed to parse json: %s", jsonPath.data());
        return nullptr;
    }
    void* shader = nullptr;
    if (name.contains(".comp"))
    {
        SDL_GPUComputePipelineCreateInfo info{};
        for (int i = 1; i < 19; i += 2)
        {
            if (tokens[i].type != JSMN_STRING)
            {
                SDL_Log("Bad json type: %s", jsonPath.data());
                return nullptr;
            }
            char* keyString = jsonData.data() + tokens[i + 0].start;
            char* valueString = jsonData.data() + tokens[i + 1].start;
            int keySize = tokens[i + 0].end - tokens[i + 0].start;
            uint32_t* value;
            if (!std::memcmp("samplers", keyString, keySize))
            {
                value = &info.num_samplers;
            }
            else if (!std::memcmp("readonly_storage_textures", keyString, keySize))
            {
                value = &info.num_readonly_storage_textures;
            }
            else if (!std::memcmp("readonly_storage_buffers", keyString, keySize))
            {
                value = &info.num_readonly_storage_buffers;
            }
            else if (!std::memcmp("readwrite_storage_textures", keyString, keySize))
            {
                value = &info.num_readwrite_storage_textures;
            }
            else if (!std::memcmp("readwrite_storage_buffers", keyString, keySize))
            {
                value = &info.num_readwrite_storage_buffers;
            }
            else if (!std::memcmp("uniform_buffers", keyString, keySize))
            {
                value = &info.num_uniform_buffers;
            }
            else if (!std::memcmp("threadcount_x", keyString, keySize))
            {
                value = &info.threadcount_x;
            }
            else if (!std::memcmp("threadcount_y", keyString, keySize))
            {
                value = &info.threadcount_y;
            }
            else if (!std::memcmp("threadcount_z", keyString, keySize))
            {
                value = &info.threadcount_z;
            }
            else
            {
                assert(false);
            }
            *value = *valueString - '0';
        }
        info.code = reinterpret_cast<Uint8*>(shaderData.data());
        info.code_size = shaderData.size();
        info.entrypoint = entrypoint;
        info.format = shaderFormat;
        shader = SDL_CreateGPUComputePipeline(device, &info);
    }
    else
    {
        SDL_GPUShaderCreateInfo info{};
        for (int i = 1; i < 9; i += 2)
        {
            if (tokens[i].type != JSMN_STRING)
            {
                SDL_Log("Bad json type: %s", jsonPath.data());
                return nullptr;
            }
            char* keyString = jsonData.data() + tokens[i + 0].start;
            char* valueString = jsonData.data() + tokens[i + 1].start;
            int keySize = tokens[i + 0].end - tokens[i + 0].start;
            uint32_t* value;
            if (!std::memcmp("samplers", keyString, keySize))
            {
                value = &info.num_samplers;
            }
            else if (!std::memcmp("storage_textures", keyString, keySize))
            {
                value = &info.num_storage_textures;
            }
            else if (!std::memcmp("storage_buffers", keyString, keySize))
            {
                value = &info.num_storage_buffers;
            }
            else if (!std::memcmp("uniform_buffers", keyString, keySize))
            {
                value = &info.num_uniform_buffers;
            }
            else
            {
                assert(false);
            }
            *value = *valueString - '0';
        }
        info.code = reinterpret_cast<Uint8*>(shaderData.data());
        info.code_size = shaderData.size();
        info.entrypoint = entrypoint;
        info.format = shaderFormat;
        if (name.contains(".frag"))
        {
            info.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
        }
        else
        {
            info.stage = SDL_GPU_SHADERSTAGE_VERTEX;
        }
        shader = SDL_CreateGPUShader(device, &info);
    }
    if (!shader)
    {
        SDL_Log("Failed to create shader: %s, %s", name.data(), SDL_GetError());
        return nullptr;
    }
    return shader;
}

SDL_GPUShader* LoadShader(SDL_GPUDevice* device, const std::string_view& name)
{
    return static_cast<SDL_GPUShader*>(Load(device, name));
}

SDL_GPUComputePipeline* LoadComputePipeline(SDL_GPUDevice* device, const std::string_view& name)
{
    return static_cast<SDL_GPUComputePipeline*>(Load(device, name));
}