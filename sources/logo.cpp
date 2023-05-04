/*
 * Copyright 2023 Wiimag Inc. All rights reserved.
 * License: https://wallet.wiimag.com/LICENSE
 */

#include "logo.h"

#if BUILD_APPLICATION

#include "eod.h"
#include "stock.h"
#include "settings.h"

#include <framework/common.h>
#include <framework/jobs.h>
#include <framework/query.h>
#include <framework/module.h>
#include <framework/shared_mutex.h>
#include <framework/handle.h>
#include <framework/session.h>
#include <framework/profiler.h>
#include <framework/string.h>
#include <framework/array.h>
#include <framework/bgfx.h>
#include <framework/system.h>

#include <foundation/fs.h>
#include <foundation/path.h>
#include <foundation/stream.h>
#include <foundation/memory.h>

#include <stb/stb_image.h>

#define HASH_LOGO static_hash_string("logo", 4, 0x66e3b93837662c88ULL)

struct logo_image_t 
{
    string_const_t extension{};
    string_table_symbol_t symbol{};
    
    int width{ 0 };
    int height{ 0 };
    int channels{ 0 };
    stbi_uc* data{ nullptr };
    stbi_uc* data_texture{ nullptr };

    uint32_t min_x = 0, min_y = 0;
    uint32_t max_x = 0, max_y = 0;
    uint32_t most_common_color{ 0 };

    status_t status{ STATUS_UNDEFINED };
    status_t thumbnail_cache_status{ STATUS_UNDEFINED };

    job_t* download_job{ nullptr };

    bgfx::TextureHandle texture = BGFX_INVALID_HANDLE;

    function<void(logo_image_t*)> image_processor;
};

 /*! The logo cache directory. */
struct logo_t
{
    hash_t key{ 0 };
    stock_handle_t stock_handle;
    string_table_symbol_t symbol{};

    logo_image_t* icon{nullptr};
    logo_image_t* banner{nullptr};
};

static logo_t* _logos = nullptr;
static shared_mutex _logos_mutex;

typedef Handle<logo_t, [](HandleKey key)->logo_t*
{
    SHARED_READ_LOCK(_logos_mutex);

    const size_t logo_count = array_size(_logos);
    if (logo_count == 0)
        return nullptr;

    if (key.index >= logo_count)
        return nullptr;

    logo_t* ptr = &_logos[key.index];
    if (ptr->key != key.hash)
        return nullptr;

    return ptr;
}, [](logo_t* ptr)->HandleKey
{
    return { size_t(ptr - _logos), ptr->key };
}> logo_handle_t;

//
// # PRIVATE
//

FOUNDATION_STATIC logo_image_t* logo_image_allocate(string_table_symbol_t code_symbol, const char* extension, size_t extension_length)
{
    logo_image_t* image = (logo_image_t*)memory_allocate(HASH_LOGO, sizeof(logo_image_t), 0, MEMORY_PERSISTENT | MEMORY_ZERO_INITIALIZED);
    image->symbol = code_symbol;
    image->extension = string_const_t{ extension, extension_length };
    image->status = STATUS_INITIALIZED;
    image->thumbnail_cache_status = STATUS_UNDEFINED;
    image->texture = BGFX_INVALID_HANDLE;
    return image;
}

FOUNDATION_STATIC void logo_image_deallocate(logo_image_t*& image)
{
    if (image->download_job)
        job_deallocate(image->download_job);

    if (bgfx::isValid(image->texture))
        bgfx::destroy(image->texture);

    if (image->data)
        stbi_image_free(image->data);
        
    memory_deallocate(image);
    image = nullptr;
}

FOUNDATION_STATIC logo_t* logo_find(hash_t logo_hash)
{
    foreach(logo, _logos)
    {
        if (logo->key == logo_hash)
            return logo;
    }

    return nullptr;
}

FOUNDATION_STATIC void logo_process_banner_image(logo_image_t* image)
{
    FOUNDATION_ASSERT(image && image->data);

    // Scan all pixel data and build a histogram of the colors
    const uint8_t* data = image->data;
    const size_t data_size = image->width * image->height * image->channels;
    const size_t histogram_size = 256 * 256 * 256;
    uint32_t* histogram = (uint32_t*)memory_allocate(HASH_LOGO, histogram_size * sizeof(uint32_t), 4, MEMORY_TEMPORARY);
    memset(histogram, 0, histogram_size * sizeof(uint32_t));
    for (size_t i = 0; i < data_size; i += image->channels)
    {
        if (image->channels == 3)
        {
            const uint32_t color = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
            ++histogram[color];
        }
        else if (image->channels == 4 && data[i + 3] != 0)
        {
            const uint32_t color = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
            ++histogram[color];
        }
    }

    // Find the most common color
    uint32_t max_color = 0;
    uint32_t max_count = 0;
    for (size_t i = 0; i < histogram_size; ++i)
    {
        if (i <= 0x00111111 || i > 0x00EEEEEE)
            continue;

        if (histogram[i] > max_count)
        {
            max_color = (uint32_t)i;
            max_count = histogram[i];
        }
    }

    memory_deallocate(histogram);

    // Flip max_color to RGB to ABGR
    image->most_common_color = rgb_to_abgr(max_color);

    const float max_color_coverage = max_count / (image->width * image->height * 1.0f) * 100.0f;
    const bool discard_bg_color = true;//max_color_coverage > 85.0f;
    // Find the image size by excluding transparent pixels        
    {
        uint32_t min_x = 0;
        uint32_t min_y = 0;
        uint32_t max_x = 0;
        uint32_t max_y = 0;
        for (uint32_t y = 0; y < to_uint(image->height); ++y)
        {
            for (uint32_t x = 0; x < to_uint(image->width); ++x)
            {
                bool discard = false;
                const uint8_t* pixels = &data[(y * image->width + x) * image->channels];
                if (image->channels == 4)
                {
                    const uint8_t a = pixels[3];
                    if (a >= 4 && max_color > 0)
                    {
                        const uint8_t r = pixels[0];
                        const uint8_t g = pixels[1];
                        const uint8_t b = pixels[2];

                        const uint32_t c = (pixels[0] << 16) | (pixels[1] << 8) | pixels[2];
                        discard = a < 4 || ((r > 0xEE && g > 0xEE && b > 0xEE)) || (max_color > 0x111111 && (r < 0x11 && g < 0x11 && b < 0x11)) || (discard_bg_color && c == max_color);
                    }
                    else
                    {
                        discard = a < 4;
                    }
                }
                else
                {
                    const uint8_t r = pixels[0];
                    const uint8_t g = pixels[1];
                    const uint8_t b = pixels[2];

                    const uint32_t c = (pixels[0] << 16) | (pixels[1] << 8) | pixels[2];
                    discard = (r > 0xEE && g > 0xEE && b > 0xEE) || (r < 0x11 && g < 0x11 && b < 0x11) || (discard_bg_color && c == max_color);
                }

                if (!discard)
                {
                    if (!min_x && !min_y)
                    {
                        min_x = x;
                        min_y = y;
                    }
                    max_x = x;
                    max_y = y;
                }
            }
        }

        const int new_width = max_x - min_x;
        int new_height = max_y - min_y;

        if (new_height >= 16 && new_height < 40 && new_width > new_height * 2)
        {
            int padding = (40 - new_height) / 2;
            min_y = to_uint(max(0, (int)min_y - padding));
            max_y = to_uint(min(image->height, (int)max_y + padding));
        }

        image->min_x = min_x;
        image->min_y = min_y;
        image->max_x = max_x;
        image->max_y = max_y;
        new_height = max_y - min_y;

        const float ratio = (1.0f - new_height / (float)image->height) * 100.0f;
        if (ratio > 20.0f && new_height > 20 && ((float)image->width / (max_y - min_y + 1) > 2.24f))
        {
            log_debugf(HASH_LOGO, STRING_CONST("Removing logo blank lines: %d (%X / %.3g) > %.3g > %s (%dx%d) > (%dx%d)"),
                image->channels, max_color, max_color_coverage, ratio, SYMBOL_CSTR(image->symbol), image->width, image->height, new_width, new_height);

            // Remove "blank" lines from data.
            image->height = max_y - min_y + 1;
            image->data_texture = image->data + (min_y * image->width * image->channels);
        }
    }
}

FOUNDATION_STATIC logo_handle_t logo_request_image(const char* symbol, size_t symbol_length)
{    
    const hash_t logo_hash = hash(symbol, symbol_length);
    
    {
        SHARED_READ_LOCK(_logos_mutex);

        // Check if the logo image is already available
        logo_t* logo = logo_find(logo_hash);

        if (logo != nullptr)
            return logo;
    }
    
    // Resolve the stock handle
    stock_handle_t stock_handle = stock_request(symbol, symbol_length, FetchLevel::FUNDAMENTALS);

    // Get the stock object.
    const stock_t* s = stock_handle;
    if (s == nullptr)
        return nullptr;

    {
        logo_t logo{};
        logo.key = logo_hash;
        logo.symbol = s->code;
        logo.stock_handle = stock_handle;

        logo.banner = logo_image_allocate(s->code, STRING_CONST("png"));
        logo.banner->image_processor = logo_process_banner_image;
        
        logo.icon = logo_image_allocate(s->code, STRING_CONST("icon.png"));
        
        SHARED_WRITE_LOCK(_logos_mutex);
        array_push_memcpy(_logos, &logo);
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

FOUNDATION_STATIC string_const_t logo_symbol_base_name(logo_image_t* image)
{
    string_const_t symbol = string_table_decode_const(image->symbol);
    return path_base_file_name(STRING_ARGS(symbol));
}

FOUNDATION_STATIC string_const_t logo_thumbnail_cached_path(logo_image_t* image)
{
    string_const_t basename = logo_symbol_base_name(image);
    return session_get_user_file_path(STRING_ARGS(basename), STRING_CONST("thumbnails"), STRING_ARGS(image->extension));
}

FOUNDATION_STATIC bool logo_thumbnail_is_cached(logo_image_t* image)
{
    if (image->thumbnail_cache_status == STATUS_AVAILABLE)
        return true;
        
    if (image->thumbnail_cache_status == STATUS_UNDEFINED)
    {
        string_const_t cache_file_path = logo_thumbnail_cached_path(image);
        if (fs_is_file(STRING_ARGS(cache_file_path)))
        {
            image->thumbnail_cache_status = STATUS_AVAILABLE;
            return true;
        }
        
        image->thumbnail_cache_status = STATUS_ERROR_NOT_AVAILABLE;
    }

    return false;
}

FOUNDATION_STATIC bool logo_download_image(logo_t* logo, logo_image_t* image)
{
    stream_t* download_stream = nullptr;
    string_const_t cache_file_path = logo_thumbnail_cached_path(image);
    const bool load_from_cache = fs_is_file(STRING_ARGS(cache_file_path));
    if (load_from_cache)
        download_stream = fs_open_file(STRING_ARGS(cache_file_path), STREAM_IN);

    if (download_stream == nullptr)
    {
        const stock_t* s = logo->stock_handle;
        if (s == nullptr)
        {
            log_warnf(HASH_LOGO, WARNING_RESOURCE, STRING_CONST("Failed to get stock for %s"), SYMBOL_CSTR(logo->symbol));
            return (image->status = STATUS_ERROR_NOT_AVAILABLE);
        }

        string_const_t url = string_table_decode_const(s->logo);
        if (url.length == 0)
        {
            log_debugf(HASH_LOGO, STRING_CONST("Failed to decode image URL for %s"), SYMBOL_CSTR(logo->symbol));

            // Try to build guess logo URL
            string_const_t basename = logo_symbol_base_name(image);
            url = string_format_static(STRING_CONST("/img/logos/US/%.*s.png"), STRING_FORMAT(basename));
        }

        // Initiate the logo download
        const char* image_url = eod_build_image_url(STRING_ARGS(url));
        log_debugf(HASH_LOGO, STRING_CONST("Downloading logo %s"), image_url);
        download_stream = query_execute_download_file(image_url);

        if (download_stream == nullptr)
            return (image->status = STATUS_ERROR_INVALID_STREAM) >= 0;

        const size_t download_size = stream_size(download_stream);
        log_infof(HASH_LOGO, STRING_CONST("Downloaded logo %s (%" PRIsize ")"), image_url, download_size);
    }

    // Rewind stream
    stream_seek(download_stream, 0, STREAM_SEEK_BEGIN);

    log_debugf(HASH_LOGO, STRING_CONST("Decoding logo %s"), string_table_decode(logo->symbol));
    stbi_io_callbacks callbacks;
    callbacks.read = logo_image_stream_read;
    callbacks.skip = logo_image_stream_skip;
    callbacks.eof = logo_image_stream_eof;
    image->data_texture = image->data = stbi_load_from_callbacks(&callbacks, download_stream, &image->width, &image->height, &image->channels, 0);

    if (image->data == nullptr)
    {
        stream_deallocate(download_stream);
        log_errorf(HASH_LOGO, ERROR_EXCEPTION, STRING_CONST("Failed to decode logo %s"), string_table_decode(logo->symbol));
        return (image->data ? (image->status = STATUS_OK) : (image->status = STATUS_ERROR_LOAD_FAILURE)) >= 0;
    }

    image->image_processor.invoke(image);

    // Load image as a texture
    FOUNDATION_ASSERT(!bgfx::isValid(image->texture));
    const bgfx::TextureFormat::Enum texture_format = image->channels == 3 ? bgfx::TextureFormat::RGB8 : (image->channels == 1 ? bgfx::TextureFormat::A8 : bgfx::TextureFormat::RGBA8);
    image->texture = bgfx::createTexture2D(
        image->width, image->height, false, 1, texture_format, 0,
        bgfx::makeRef(image->data_texture, image->width * image->height * image->channels));

    image->status = STATUS_OK;
    log_debugf(HASH_LOGO, STRING_CONST("Loaded logo %s (%dx%dx%d)"), string_table_decode(logo->symbol), image->width, image->height, image->channels);

    // Save logo to cache
    if (!load_from_cache)
    {
        stream_t* cache_file_stream = fs_open_file(STRING_ARGS(cache_file_path), STREAM_CREATE | STREAM_OUT | STREAM_BINARY | STREAM_TRUNCATE);
        if (cache_file_stream != nullptr)
        {
            log_debugf(HASH_LOGO, STRING_CONST("Caching logo to %.*s"), STRING_FORMAT(cache_file_path));
            stream_seek(download_stream, 0, STREAM_SEEK_BEGIN);
            stream_copy(download_stream, cache_file_stream);
            stream_deallocate(cache_file_stream);
        }
    }

    stream_deallocate(download_stream);

    return image->status >= 0;
}

FOUNDATION_STATIC int logo_download_thread(void* payload)
{
    MEMORY_TRACKER(HASH_LOGO);
    SHARED_READ_LOCK(_logos_mutex);

    logo_image_t* image = (logo_image_t*)payload;
    if (image == nullptr)
        return STATUS_ERROR_INVALID_HANDLE;
    
    string_const_t logo_symbol = string_table_decode_const(image->symbol);
    hash_t logo_key = string_hash(STRING_ARGS(logo_symbol));
    logo_t* logo = logo_find(logo_key);
    if (logo == nullptr)
        return STATUS_ERROR_INVALID_HANDLE;

    if (!logo_download_image(logo, image))
    {
        log_debugf(HASH_LOGO, STRING_CONST("Failed to download logo %s.%.*s"),
            string_table_decode(logo->symbol), STRING_FORMAT(image->extension));
        return -1;
    }
        
    return 0;
}

FOUNDATION_STATIC bool logo_resolve_image(logo_image_t* image)
{
    SHARED_READ_LOCK(_logos_mutex);

    if (image->status <= STATUS_ERROR)
        return false;

    // Cleanup any finished download job
    if (image->download_job && job_completed(image->download_job))
        job_deallocate(image->download_job);

    // Check if the logo image is already available
    if (image->status == STATUS_OK)
        return true;

    if (image->status == STATUS_RESOLVING)
        return false;
        
    if (!logo_thumbnail_is_cached(image))
    {
        string_const_t logo_symbol = string_table_decode_const(image->symbol);
        hash_t logo_key = string_hash(STRING_ARGS(logo_symbol));
        logo_t* logo = logo_find(logo_key);
        if (logo == nullptr)
            return false;
            
        // Resolve the stock handle
        const stock_t* s = logo->stock_handle;
        if (s == nullptr)
            return false;
            
        if (!s->has_resolve(FetchLevel::FUNDAMENTALS))
            return false;
    }
       
    // Initiate the logo download
    if (image->download_job == nullptr)
    {
        image->status = STATUS_RESOLVING;
        image->download_job = job_execute(logo_download_thread, image);
        if (image->download_job == nullptr)
            return (image->status = STATUS_ERROR_FAILED_CREATE_JOB) >= 0;
    }

    return image->status == STATUS_OK && bgfx::isValid(image->texture);
}

FOUNDATION_STATIC ImU32 logo_transparent_background_color(const logo_image_t* image, const uint8_t* pixels)
{
    const uint8_t a = pixels[3];

    if (a < 10)
    { // ABGR
        const uint8_t r = image->most_common_color & 0xFF;
        const uint8_t g = (image->most_common_color & 0xFF00) >> 8;
        const uint8_t b = (image->most_common_color & 0xFF0000) >> 16;

        if (((r / 255.0f) * 0.299f + (g / 255.0f) * 0.587f + (b / 255.0f) * 0.114f) * 255.0f > 116.0f)
        {
            if (r > g && r > b)
                return 0xCC111122;
            if (g > r && g > b)
                return 0xDD334433;
            if (b > r && b > g)
                return 0xFF221111;
            return 0xFF111111;
        }

        if (r > g && r > b)
            return 0xCCDADAEE;
        if (g > r && g > b)
            return 0xDDEEFFEE;
        if (b > r && b > g)
            return 0xFFFFEEEE;
        return 0xFFFFFFFF;
    }

    {
        const uint8_t r = pixels[0];
        const uint8_t g = pixels[1];
        const uint8_t b = pixels[2];
        return ((b << 24) | (g << 16) | (r << 8) || a);
    }
}

FOUNDATION_STATIC ImU32 logo_get_fill_color(const logo_image_t* image)
{
    const uint8_t r = image->most_common_color & 0xFF;
    const uint8_t g = (image->most_common_color & 0xFF00) >> 8;
    const uint8_t b = (image->most_common_color & 0xFF0000) >> 16;

    if ((r >= 0xAA && g >= 0xAA && b >= 0xAA) || (r < 0x11 && g < 0x11 && b < 0x11))
        return image->most_common_color;

    FOUNDATION_ASSERT(image->data);
    const uint8_t* pixels = &image->data[0];
    if (image->channels == 3 || pixels[3] == 0xFF)
        return (0xFF000000 | (pixels[2] << 16) | (pixels[1] << 8) | (pixels[0] << 0));

    if (pixels[3] == 0xFF)
        return (0x000000FF | (pixels[2] << 24) | (pixels[1] << 16) | (pixels[0] << 8));

    return logo_transparent_background_color(image, pixels);
}

FOUNDATION_STATIC bool logo_icon_selected_callback(string_const_t symbol, string_const_t icon_path)
{
    // Get thumbnail directory
    string_const_t thumbnail_dir = session_get_user_file_path(STRING_CONST("thumbnails"));

    // Parse symbol to only keep the code (removing *.EXCHANGE)
    string_const_t symbol_code = symbol;
    size_t dot_pos = string_find(STRING_ARGS(symbol), '.', 0);
    if (dot_pos != STRING_NPOS)
        symbol_code.length = dot_pos;

    // Format destination path
    string_t dest_path = string_format(SHARED_BUFFER(BUILD_MAX_PATHLEN),
        STRING_CONST("%.*s/%.*s.icon.png"), STRING_FORMAT(thumbnail_dir), STRING_FORMAT(symbol_code));

    // Copy the icon in the thumbnail cache
    if (fs_copy_file(STRING_ARGS(icon_path), STRING_ARGS(dest_path)))
    {
        // Update the thumbnail cache
        const hash_t logo_hash = string_hash(STRING_ARGS(symbol));
        logo_t* logo = logo_find(logo_hash);
        if (logo)
        {
            logo_image_deallocate(logo->icon);

            string_table_symbol_t scode = string_table_encode(STRING_ARGS(symbol));
            logo->icon = logo_image_allocate(scode, STRING_CONST("icon.png"));
            return true;
        }
        
        return logo_request_image(STRING_ARGS(symbol));
    }

    return false;
}

FOUNDATION_STATIC bool logo_banner_selected_callback(string_const_t symbol, string_const_t icon_path)
{
    // Get thumbnail directory
    string_const_t thumbnail_dir = session_get_user_file_path(STRING_CONST("thumbnails"));

    // Parse symbol to only keep the code (removing *.EXCHANGE)
    string_const_t symbol_code = symbol;
    size_t dot_pos = string_find(STRING_ARGS(symbol), '.', 0);
    if (dot_pos != STRING_NPOS)
        symbol_code.length = dot_pos;

    // Format destination path
    string_t dest_path = string_format(SHARED_BUFFER(BUILD_MAX_PATHLEN), 
        STRING_CONST("%.*s/%.*s.png"), STRING_FORMAT(thumbnail_dir), STRING_FORMAT(symbol_code));

    // Copy the icon in the thumbnail cache
    SHARED_WRITE_LOCK(_logos_mutex);
    if (fs_copy_file(STRING_ARGS(icon_path), STRING_ARGS(dest_path)))
    {
        // Update the thumbnail cache
        const hash_t logo_hash = string_hash(STRING_ARGS(symbol));
        logo_t* logo = logo_find(logo_hash);
        if (logo)
        {
            logo_image_deallocate(logo->banner);

            string_table_symbol_t scode = string_table_encode(STRING_ARGS(symbol));
            logo->banner = logo_image_allocate(scode, STRING_CONST("png"));
            logo->banner->image_processor = logo_process_banner_image;

            return true;
        }
        
        return logo_request_image(STRING_ARGS(symbol));
    }

    return false;
}

//
// # PUBLIC API
//

bool logo_render_icon(const char* symbol, size_t symbol_length, ImVec2& rendered_size, bool background /*= false*/, bool show_tooltip /*= true*/, ImRect* fill_rect /*= nullptr*/)
{
    MEMORY_TRACKER(HASH_LOGO);

    // Request logo image
    logo_handle_t logo_handle = logo_request_image(symbol, symbol_length);
    if (!logo_handle)
        return false;

    if (!logo_resolve_image(logo_handle->icon))
        return false;

    // Get logo image texture
    int width = 0, height = 0, channels = 0;
    int banner_width = 0, banner_height = 0, banner_channels = 0;
    uint32_t banner_color = 0;
    ImU32 bg_logo_banner_color = 0;
    bgfx::TextureHandle texture = BGFX_INVALID_HANDLE, banner_texture;
    {
        SHARED_READ_LOCK(_logos_mutex);

        const logo_t* logo = logo_handle;
        if (logo)
        {
            width = logo->icon->width;
            height = logo->icon->height;
            channels = logo->icon->channels;
            texture = logo->icon->texture;
            
            banner_width = logo->banner->width;
            banner_height = logo->banner->height;
            banner_channels = logo->banner->channels;
            banner_texture = logo->banner->texture;
            banner_color = logo->banner->most_common_color;

            if (channels == 4 && background)
                bg_logo_banner_color = logo_transparent_background_color(logo->icon, logo->icon->data);
        }
    }

    if (!bgfx::isValid(texture))
        return false;

    if (rendered_size.x == 0)
    {
        rendered_size.x = ImGui::GetContentRegionAvail().x;
        const float hratio = rendered_size.x / width;
        rendered_size.y = height * hratio;
    }
    ImVec2 spos = ImGui::GetCursorScreenPos();
    spos.y = (float)math_ceil(spos.y + 0.5f);

    if (channels == 3)
        rendered_size.y = (float)math_floor(rendered_size.y + 0.5f);
    const ImRect logo_rect(spos, spos + rendered_size);
    
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(logo_rect.Min, logo_rect.Max, true);
    if (channels == 4 && background)
        dl->AddRectFilled(logo_rect.Min, logo_rect.Max, bg_logo_banner_color); // ABGR

    // Fit the width and height in the rendered size by keeping the aspect ratio and centering the image
    const float hratio = rendered_size.x / (width);
    const float vratio = rendered_size.y / (height);
    const float ratio = hratio < vratio ? hratio : vratio;
    const float w = math_floor(width * ratio);
    const float x = (rendered_size.x - w) * 0.5f;
    const float h = math_floor(height * ratio);
    const float y = (rendered_size.y - h) * 0.5f;

    const float yoffset = channels == 3 ? 0.0 : 2.0f;
    const float hoffset = channels == 3 ? 0.0 : 4.0f;

    dl->AddImage((ImTextureID)(intptr_t)texture.idx, logo_rect.Min + ImVec2(x, y - yoffset), logo_rect.Min + ImVec2(x + w, y + h - hoffset));
    dl->PopClipRect();

    if (fill_rect)
        *fill_rect = logo_rect;
    if (show_tooltip && ImGui::IsWindowFocused() && ImGui::IsMouseHoveringRect(logo_rect.Min, logo_rect.Max))
    {
        logo_resolve_image(logo_handle->banner);

        ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 0);

        if (banner_channels == 4)
            ImGui::PushStyleColor(ImGuiCol_PopupBg, (ImU32)imgui_color_text_for_background(banner_color));
        if (ImGui::BeginTooltip())
        {
            if (bgfx::isValid(banner_texture))
                ImGui::Image((ImTextureID)(intptr_t)banner_texture.idx, ImVec2(banner_width, banner_height));
            else
                ImGui::Image((ImTextureID)(intptr_t)texture.idx, ImVec2(width, height));
            ImGui::EndTooltip();
        }
        if (banner_channels == 4)
            ImGui::PopStyleColor();

        ImGui::PopStyleVar();
    }

    return true;
}

bool logo_render_banner(const char* symbol, size_t symbol_length, ImVec2& rendered_size, bool background /*= false*/, bool show_tooltip /*= true*/, ImRect* fill_rect /*= nullptr*/)
{
    MEMORY_TRACKER(HASH_LOGO);

    // Request logo image
    logo_handle_t logo_handle = logo_request_image(symbol, symbol_length);
    if (!logo_handle)
        return false;

    if (!logo_resolve_image(logo_handle->banner))
        return false;

    // Get logo image texture
    int width = 0, height = 0, channels = 0;
    uint32_t banner_color = 0;
    bgfx::TextureHandle texture = BGFX_INVALID_HANDLE;
    {
        SHARED_READ_LOCK(_logos_mutex);
        const logo_t* image = logo_handle;
        if (image)
        {
            width = image->banner->width;
            height = image->banner->height;
            channels = image->banner->channels;
            texture = image->banner->texture;
            banner_color = image->banner->most_common_color;
        }
    }

    if (!bgfx::isValid(texture))
        return false;

    if (rendered_size.x == 0)
    {
        rendered_size.x = ImGui::GetContentRegionAvail().x;
        const float hratio = rendered_size.x / width;
        rendered_size.y = height * hratio;
    }
    ImVec2 spos = ImGui::GetCursorScreenPos();
    spos.y = (float)math_ceil(spos.y + 0.5f);

    if (channels == 3)
        rendered_size.y = (float)math_floor(rendered_size.y + 0.5f);
    const ImRect logo_rect(spos, spos + rendered_size);

    const ImU32 bg_logo_banner_color = imgui_color_text_for_background(banner_color);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    dl->PushClipRect(logo_rect.Min, logo_rect.Max, true);
    if (channels == 4 && background)
        dl->AddRectFilled(logo_rect.Min, logo_rect.Max, bg_logo_banner_color); // ABGR

    // Fit the width and height in the rendered size by keeping the aspect ratio and centering the image
    const float hratio = rendered_size.x / (width);
    const float vratio = rendered_size.y / (height);
    const float ratio = hratio < vratio ? hratio : vratio;
    const float w = math_floor(width * ratio);
    const float x = 0.0f;//(rendered_size.x - w) * 0.5f;
    const float h = math_floor(height * ratio);
    const float y = (rendered_size.y - h) * 0.5f;

    const float yoffset = channels == 3 ? 0.0 : 2.0f;
    const float hoffset = channels == 3 ? 0.0 : 4.0f;

    dl->AddImage((ImTextureID)(intptr_t)texture.idx, logo_rect.Min + ImVec2(x, y - yoffset), logo_rect.Min + ImVec2(x + w, y + h - hoffset));
    dl->PopClipRect();
    
    if (fill_rect)
        *fill_rect = logo_rect;
    if (show_tooltip && ImGui::IsWindowFocused() && ImGui::IsMouseHoveringRect(logo_rect.Min, logo_rect.Max))
    {
        if (channels == 4)
            ImGui::PushStyleColor(ImGuiCol_PopupBg, bg_logo_banner_color);
        if (ImGui::BeginTooltip())
        {
            ImGui::Image((ImTextureID)(intptr_t)texture.idx, ImVec2(width, height));
            ImGui::EndTooltip();
        }
        if (channels == 4)
            ImGui::PopStyleColor();
    }

    return true;
}

bool logo_select_icon(const char* symbol, size_t length)
{
    string_const_t symbolstr = string_const(symbol, length);
    const char* title = tr_format("Select icon image (i.e. 32x32) for {0}...", symbolstr);
    return system_open_file_dialog(title, "PNG (*.png)|*.png", 
        nullptr, L1(logo_icon_selected_callback(symbolstr, _1)));
}

bool logo_select_banner(const char* symbol, size_t length)
{
    string_const_t symbolstr = string_const(symbol, length);
    const char* title = tr_format("Select image banner (i.e. 200x32) for {0}...", symbolstr);
    return system_open_file_dialog(title, "PNG (*.png)|*.png", 
        nullptr, L1(logo_banner_selected_callback(symbolstr, _1)));
}

bool logo_has_banner(const char* symbol, size_t symbol_length, int& banner_width, int& banner_height, int& banner_channels, 
    ImU32& image_bg_color, ImU32& fill_color)
{
    // Request logo image
    logo_handle_t logo_handle = logo_request_image(symbol, symbol_length);
    if (!logo_handle)
        return false;

    logo_resolve_image(logo_handle->banner);

    // Get logo image texture
    {
        SHARED_READ_LOCK(_logos_mutex);
        const logo_t* logo = logo_handle;
        if (logo == nullptr || logo->banner->data == nullptr)
        {
            banner_width = logo->icon->width;
            banner_height = logo->icon->height;
            banner_channels = logo->icon->channels;
            fill_color = image_bg_color = logo_get_fill_color(logo->icon);

            return false;
        }
        banner_width = logo->banner->width;
        banner_height = logo->banner->height;
        banner_channels = logo->banner->channels;
        image_bg_color = logo->banner->most_common_color;
        fill_color = logo_get_fill_color(logo->banner);

        if ((float)logo->banner->width / logo->banner->height > 2.24f)
            return true;
    }

    return false;
}

bool logo_render_banner(const char* symbol, size_t symbol_length, const ImRect& rect, ImU32* suggested_text_color /*= nullptr*/)
{
    bool selected = false;
    ImGui::PushStyleCompact();
    string_const_t code = string_const(symbol, symbol_length);

    bool can_show_banner = SETTINGS.show_logo_banners && !ImGui::IsKeyDown(ImGuiKey_B);
    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec2& space = rect.GetSize();
    const ImVec2& text_size = ImGui::CalcTextSize(STRING_RANGE(code));
    const float button_width = text_size.y;

    int logo_banner_width = 0, logo_banner_height = 0, logo_banner_channels = 0;
    ImU32 logo_banner_color = 0xFFFFFFFF, fill_color = 0xFFFFFFFF;
    if (logo_has_banner(STRING_ARGS(code),
        logo_banner_width, logo_banner_height, logo_banner_channels, logo_banner_color, fill_color) &&
        can_show_banner &&
        (logo_banner_width / (logo_banner_height / text_size.y)) > space.x * 0.3f)
    {
        const float ratio = logo_banner_height / text_size.y;
        logo_banner_height = text_size.y;
        logo_banner_width /= ratio;

        if (logo_banner_channels == 4)
        {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImColor bg_logo_banner_color = fill_color;
            dl->AddRectFilled(rect.Min, rect.Max, fill_color);

            const ImU32 best_text_color = imgui_color_text_for_background(fill_color); 
            ImGui::PushStyleColor(ImGuiCol_Text, best_text_color);
            if (suggested_text_color)
                *suggested_text_color = best_text_color;
        }
        else if (logo_banner_channels == 3)
        {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(rect.Min, rect.Max, fill_color);

            const ImU32 best_text_color = imgui_color_text_for_background(fill_color);
            ImGui::PushStyleColor(ImGuiCol_Text, best_text_color);
            
            if (suggested_text_color)
                *suggested_text_color = best_text_color;
        }
        else
        {
            FOUNDATION_ASSERT_FAIL("Invalid logo banner channels");
        }

        const float height_scale = logo_banner_channels == 4 ? 1.0f : rect.GetHeight() / logo_banner_height;
        if (logo_banner_channels == 3)
            ImGui::MoveCursor(-style.FramePadding.x, -style.FramePadding.y - 1.0f, false);
        ImVec2 logo_size = ImVec2(logo_banner_width * height_scale, logo_banner_height * height_scale);
        if (!logo_render_banner(STRING_ARGS(code), logo_size, false, false))
        {
            ImGui::TextUnformatted(STRING_RANGE(code));
        }
        else
        {
            if (logo_banner_channels == 3)
                ImGui::MoveCursor(style.FramePadding.x, style.FramePadding.y + 1.0f, false);
            ImGui::Dummy(ImVec2(logo_banner_width, logo_banner_height));
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                selected = true;
        }

        if (ImGui::IsItemHovered())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, 0xFFEEEEEE);
            ImGui::SetTooltip("%.*s", STRING_FORMAT(code));
            ImGui::PopStyleColor();
        }

        ImGui::PopStyleColor();
    }
    else
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (logo_banner_width > 0)
        {
            dl->AddRectFilled(rect.Min, rect.Max, logo_banner_color); // ABGR
            const ImU32 best_text_color = imgui_color_text_for_background(logo_banner_color);
            ImGui::PushStyleColor(ImGuiCol_Text, best_text_color);

            if (suggested_text_color)
                *suggested_text_color = best_text_color;
        }

        const float code_width = text_size.x + (style.ItemSpacing.x * 2.0f);
        ImGui::TextUnformatted(STRING_RANGE(code));
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            selected = true;

        ImVec2 logo_size(IM_SCALEF(18.0f), IM_SCALEF(18.0f));

        const float as = (rect.GetSize().y - IM_SCALEF(4.0f)) / logo_size.y;
        logo_size.x *= as;
        logo_size.y *= as;

        float space_left = rect.GetWidth() - code_width;
        ImGui::MoveCursor(space_left - logo_size.x + IM_SCALEF(2.0f), 0, true);
        logo_render_icon(STRING_ARGS(code), logo_size, true, true);
        ImGui::Dummy(logo_size);

        if (logo_banner_width > 0)
            ImGui::PopStyleColor();
    }

    ImGui::PopStyleCompact();

    return selected;
}

//
// # SYSTEM
//

FOUNDATION_STATIC void logo_initialize()
{
    string_const_t query_cache_path = session_get_user_file_path(STRING_CONST("thumbnails"));
    fs_make_directory(STRING_ARGS(query_cache_path));
}

FOUNDATION_STATIC void logo_shutdown()
{
    foreach(logo, _logos)
    {
        logo_image_deallocate(logo->icon);
        logo_image_deallocate(logo->banner);
    }
    array_deallocate(_logos);
}

DEFINE_MODULE(LOGO, logo_initialize, logo_shutdown, MODULE_PRIORITY_UI);

#endif // BUILD_APPLICATION
