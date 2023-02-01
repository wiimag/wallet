/*
 * Copyright 2023 equals-forty-two.com All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#include "logo.h"

#include "eod.h"
#include "stock.h"

#include <framework/jobs.h>
#include <framework/query.h>
#include <framework/service.h>
#include <framework/shared_mutex.h>
#include <framework/handle.h>

#include <foundation/stream.h>

#include <stb/stb_image.h>

#include <bgfx/bgfx.h>

#define HASH_LOGO static_hash_string("logo", 4, 0x66e3b93837662c88ULL)

struct logo_image_t
{
    hash_t key{ 0 };
    
    string_table_symbol_t url{};
    string_table_symbol_t symbol{};
    
    int width{ 0 };
    int height{ 0 };
    int channels{ 0 };
    stbi_uc* data{ nullptr };

    stock_handle_t stock_handle;
    status_t status{ STATUS_UNDEFINED };

    job_t* download_job{ nullptr };

    bgfx::TextureHandle texture = BGFX_INVALID_HANDLE;

};

static shared_mutex _logos_mutex;
static logo_image_t* _logos = nullptr;

typedef Handle<logo_image_t, [](HandleKey key)->logo_image_t*
{
    SHARED_READ_LOCK(_logos_mutex);

    const size_t logo_count = array_size(_logos);
    if (logo_count == 0)
        return nullptr;

    if (key.index >= logo_count)
        return nullptr;

    logo_image_t* ptr = &_logos[key.index];
    if (ptr->key != key.hash)
        return nullptr;

    return ptr;
}, [](logo_image_t* ptr)->HandleKey
{
    return { size_t(ptr - _logos), ptr->key };
}> logo_handle_t;

//
// # PRIVATE
//

void logo_render_command(const ImDrawList* parent_list, const ImDrawCmd* cmd)
{
    FOUNDATION_ASSERT(cmd->UserCallbackData);

//     const infineis_viewport_t* viewport = (infineis_viewport_t*)cmd->UserCallbackData;
//     if (viewport->frame == nullptr || viewport->frame->samples == nullptr)
//         return;
// 
//     infineis_frame_t* frame = viewport->frame;
//     const bgfx::ViewId dcm_view = 1;
//     const infineis_viewport_desc_t& desc = viewport->desc;
//     const ImRect& rect = desc.rect;
//     const auto wsize = rect.GetSize();
//     const float frame_width = frame->width * desc.scale[0];
//     const float frame_height = frame->height * desc.scale[1];
//     const ImVec2 offset = desc.offset;
// 
//     const float scx = viewport->scroll_offset.x;
//     const float scy = viewport->scroll_offset.y;
// 
//     if (!bgfx::isValid(frame->dcm_texture))
//     {
//         const int16_t* samples = frame->samples;
//         const uint32_t sample_count = array_size(samples);
// 
//         bgfx::TextureFormat::Enum texture_format = bgfx::TextureFormat::R16S;
//         frame->dcm_texture = bgfx::createTexture2D(
//             frame->width, frame->height, false, 1, texture_format, 0,
//             bgfx::makeRef(frame->samples, sample_count * sizeof(frame->samples[0])));
//     }
// 
//     float scrolling_position[16], scale[16], transform[16], moffset[16], tmp[16];
//     bx::mtxTranslate(moffset, rect.Min.x + offset.x, rect.Min.y + offset.y, 0);
//     bx::mtxTranslate(scrolling_position, -scx, -scy, 0);
//     bx::mtxScale(scale, frame_width, frame_height, 1);
//     bx::mtxMul(tmp, scale, scrolling_position);
//     bx::mtxMul(transform, tmp, moffset);
//     bgfx::setTransform(transform);
// 
//     const uint16_t xx = (uint16_t)bx::max(cmd->ClipRect.x, 0.0f);
//     const uint16_t yy = (uint16_t)bx::max(cmd->ClipRect.y, 0.0f);
//     const uint16_t ww = (uint16_t)bx::min(cmd->ClipRect.z, 65535.0f) - xx;
//     const uint16_t hh = (uint16_t)bx::min(cmd->ClipRect.w, 65535.0f) - yy;
//     bgfx::setScissor(xx, yy, ww, hh);
// 
//     infineis_frame_window_properties_t window_properties;
//     window_properties.level = desc.center;
//     window_properties.window = desc.window;
//     window_properties.rescale_intercept = frame->rescale_intercept;
//     window_properties.pixel_max = (float)(1 << (frame->bits_allocated - 1));
// 
//     infineis_frame_render_options_t render_options;
//     render_options.dynamic = desc.full_dynamic ? 1.0f : 0;
// 
//     bgfx::setUniform(_bgfx_dcm_frame_options_uniform, &render_options, UINT16_MAX);
//     bgfx::setUniform(_bgfx_dcm_frame_window_uniform, &window_properties, UINT16_MAX);
// 
//     uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_MSAA |
//         BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA);
// 
//     FOUNDATION_ASSERT(bgfx::isValid(frame->dcm_texture));
//     bgfx::setState(state);
//     bgfx::setTexture(0, _bgfx_dcm_frame_tex_color, frame->dcm_texture);
//     bgfx::setVertexBuffer(0, _bgfx_dcm_frame_vertex_buffer);
//     bgfx::setIndexBuffer(_bgfx_dcm_frame_index_buffer);
// 
//     bgfx::submit(dcm_view, _bgfx_dcm_frame_shader_program);
}

FOUNDATION_STATIC logo_image_t* logo_find_image(hash_t logo_hash)
{
    foreach(img, _logos)
    {
        if (img->key == logo_hash)
            return img;
    }

    return nullptr;
}

FOUNDATION_STATIC logo_handle_t logo_request_image(const char* symbol, size_t symbol_length)
{    
    const hash_t logo_hash = hash(symbol, symbol_length);
    
    {
        SHARED_READ_LOCK(_logos_mutex);

        // Check if the logo image is already available
        logo_image_t* logo_image = logo_find_image(logo_hash);

        if (logo_image != nullptr)
            return logo_image;
    }
    
    // Resolve the stock handle
    stock_handle_t stock_handle = stock_request(symbol, symbol_length, FetchLevel::FUNDAMENTALS);

    // Get the stock object.
    const stock_t* s = stock_handle;
    if (s == nullptr)
        return nullptr;

    {
        logo_image_t limg{};
        limg.key = logo_hash;
        limg.symbol = s->code;
        limg.stock_handle = stock_handle;

        SHARED_WRITE_LOCK(_logos_mutex);
        array_push_memcpy(_logos, &limg);
        return array_last(_logos);
    }
}

FOUNDATION_STATIC int logo_image_stream_read(void* user, char* data, int size)
{
    stream_t* stream = (stream_t*)user;
    FOUNDATION_ASSERT(stream);

    const size_t byte_read = stream_read(stream, data, size);
    return to_int(byte_read);
}

FOUNDATION_STATIC void logo_image_stream_skip(void* user, int n)
{
    stream_t* stream = (stream_t*)user;
    FOUNDATION_ASSERT(stream);
    stream_seek(stream, n, STREAM_SEEK_CURRENT);
}

FOUNDATION_STATIC int logo_image_stream_eof(void* user)
{
    stream_t* stream = (stream_t*)user;
    FOUNDATION_ASSERT(stream);
    return stream_eos(stream) ? 1 : 0;
}

FOUNDATION_STATIC int logo_image_download(void* payload)
{
    SHARED_READ_LOCK(_logos_mutex);
    
    hash_t image_key = (hash_t)payload;
    logo_image_t* image = logo_find_image(image_key);
    if (image == nullptr)
        return STATUS_ERROR_INVALID_HANDLE;
        
    string_const_t url = string_table_decode_const(image->url);
    if (url.length == 0)
    {
        log_errorf(HASH_LOGO, ERROR_EXCEPTION, STRING_CONST("Failed to decode image URL for %s"), SYMBOL_CSTR(image->symbol));
        return (image->status = STATUS_ERROR_INVALID_REQUEST);
    }

    // Initiate the logo download
    const char* image_url = eod_build_image_url(STRING_ARGS(url));
    log_infof(HASH_LOGO, STRING_CONST("Downloading logo %s"), image_url);
    
    stream_t* download_stream = query_execute_download_file(image_url);
    if (download_stream == nullptr)
    {
        log_errorf(HASH_LOGO, ERROR_EXCEPTION, STRING_CONST("Failed to download logo %s"), image_url);
        return (image->status = STATUS_ERROR_INVALID_STREAM);
    }

    const size_t download_size = stream_size(download_stream);
    log_infof(HASH_LOGO, STRING_CONST("Downloaded logo %s (%" PRIsize ")"), image_url, download_size);
        
    stbi_io_callbacks callbacks;
    callbacks.read = logo_image_stream_read;
    callbacks.skip = logo_image_stream_skip;
    callbacks.eof = logo_image_stream_eof;

    // Rewind stream
    stream_seek(download_stream, 0, STREAM_SEEK_BEGIN);

    log_infof(HASH_LOGO, STRING_CONST("Decoding logo %s"), image_url);
    image->data = stbi_load_from_callbacks(&callbacks, download_stream, &image->width, &image->height, &image->channels, 0);
    stream_deallocate(download_stream);

    if (image->data == nullptr)
    {
        log_errorf(HASH_LOGO, ERROR_EXCEPTION, STRING_CONST("Failed to decode logo %s"), image_url);
        return image->data ? (image->status = STATUS_OK) : (image->status = STATUS_ERROR_LOAD_FAILURE);
    }
        
    log_infof(HASH_LOGO, STRING_CONST("Decoded logo %s (%dx%dx%d)"), image_url, image->width, image->height, image->channels);

    // Load image as a texture
    FOUNDATION_ASSERT(!bgfx::isValid(image->texture));
    const bgfx::TextureFormat::Enum texture_format = image->channels == 3 ? bgfx::TextureFormat::RGB8 : bgfx::TextureFormat::RGBA8;
    image->texture = bgfx::createTexture2D(
        image->width, image->height, false, 1, texture_format, 0,
        bgfx::makeRef(image->data, image->width * image->height * image->channels));
    
    return (image->status = STATUS_OK);
}

FOUNDATION_STATIC bool logo_resolve_image(logo_handle_t handle)
{
    SHARED_READ_LOCK(_logos_mutex);

    // Resolve image
    logo_image_t* image = handle;

    if (image->status <= STATUS_ERROR)
        return false;
    
    // Resolve the stock handle
    const stock_t* s = image->stock_handle;
    if (s == nullptr)
        return false;

    // Cleanup any finished download job
    if (image->download_job && job_completed(image->download_job))
        job_deallocate(image->download_job);

    // Check if the logo image is already available
    if (image->status == STATUS_OK)
        return true;

    if (image->status == STATUS_RESOLVING)
        return false;

    if (!s->has_resolve(FetchLevel::FUNDAMENTALS))
        return false;

    // Check if we have an URL to download the image data.
    if (s->logo == STRING_TABLE_NULL_SYMBOL)
        return (image->status = STATUS_ERROR_INVALID_REQUEST) >= 0;
       
    // Initiate the logo download
    image->url = s->logo;
    if (image->download_job == nullptr)
    {
        image->status = STATUS_RESOLVING;
        image->download_job = job_execute(logo_image_download, (void*)image->key);
        if (image->download_job == nullptr)
            return (image->status = STATUS_ERROR_FAILED_CREATE_JOB) >= 0;
    }

    return image->status == STATUS_OK && bgfx::isValid(image->texture);
}

//
// # PUBLIC API
//

bool logo_render(const char* symbol, size_t symbol_length, const ImVec2& size /*= ImVec2(0, 0)*/)
{
    // Request logo image
    logo_handle_t logo_handle = logo_request_image(symbol, symbol_length);
    if (!logo_handle)
        return false;

    if (!logo_resolve_image(logo_handle))
        return false;

    // Get logo image texture
    int width = 0, height = 0;
    bgfx::TextureHandle texture = BGFX_INVALID_HANDLE;
    {
        SHARED_READ_LOCK(_logos_mutex);
        const logo_image_t* image = logo_handle;
        if (image)
        {
            width = image->width;
            height = image->height;
            texture = image->texture;
        }
    }

    if (!bgfx::isValid(texture))
        return false;

    ImGui::Image((ImTextureID)texture.idx, size);
    if (ImGui::IsItemHovered())
    {
        ImGui::PushStyleColor(ImGuiCol_PopupBg, 0xFFFFFFFF);
        ImGui::BeginTooltip();
        ImGui::Image((ImTextureID)texture.idx, ImVec2(width, height));
        ImGui::EndTooltip();
        ImGui::PopStyleColor();
    }

    return true;
}

//
// # SYSTEM
//

FOUNDATION_STATIC void logo_initialize()
{
    
}

FOUNDATION_STATIC void logo_shutdown()
{
    foreach(img, _logos)
    {
        if (img->download_job)
            job_deallocate(img->download_job);

        if (bgfx::isValid(img->texture))
            bgfx::destroy(img->texture);

        if (img->data)
            stbi_image_free(img->data);
    }
    array_deallocate(_logos);
}

DEFINE_SERVICE(LOGO, logo_initialize, logo_shutdown, SERVICE_PRIORITY_UI);
