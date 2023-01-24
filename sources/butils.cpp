/*
 * Copyright 2022 Infineis Inc. All rights reserved.
 * License: https://infineis.com/LICENSE
 */

#include "butils.h"

#include <foundation/fs.h>
#include <foundation/string.h>
#include <foundation/assert.h>

#include <stdio.h>

bgfx::ShaderHandle load_shader(const char* filename)
{
    const char* shader_path = "???";

    switch (bgfx::getRendererType())
    {
        case bgfx::RendererType::Noop:
        case bgfx::RendererType::Direct3D9:  shader_path = "shaders/dx9/";   break;
        case bgfx::RendererType::Direct3D11:
        case bgfx::RendererType::Direct3D12: shader_path = "shaders/dx11/";  break;
        case bgfx::RendererType::Gnm:        shader_path = "shaders/pssl/";  break;
        case bgfx::RendererType::Metal:      shader_path = "../../../shaders/metal/"; break;
        case bgfx::RendererType::OpenGL:     shader_path = "shaders/glsl/";  break;
        case bgfx::RendererType::OpenGLES:   shader_path = "shaders/essl/";  break;
        case bgfx::RendererType::Vulkan:     shader_path = "shaders/spirv/"; break;
    
        default:
            FOUNDATION_ASSERT(!"Not supported");
            break;
    }

    size_t shader_path_length = strlen(shader_path);
    size_t filename_length = strlen(filename);
    char* file_path = (char*)malloc(shader_path_length + filename_length);
    memcpy(file_path, shader_path, shader_path_length);
    memcpy(&file_path[shader_path_length], filename, filename_length + 1);

    FILE* file = fopen(file_path, "rb");
    fseek(file, 0, SEEK_END);
    long fileSize = ftell(file);
    fseek(file, 0, SEEK_SET);

    const bgfx::Memory* mem = bgfx::alloc((uint32_t)fileSize + 1);
    fread(mem->data, 1, fileSize, file);
    mem->data[mem->size - 1] = '\0';
    fclose(file);

    return bgfx::createShader(mem);
}
