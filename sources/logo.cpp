/*
 * Copyright 2023 equals-forty-two.com All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#include "logo.h"

#include "eod.h"

#include <framework/service.h>

#define HASH_LOGO static_hash_string("logo", 4, 0x66e3b93837662c88ULL)

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

//
// # PUBLIC API
//

bool logo_render(const char* symbol, size_t symbol_length, const ImVec2& size /*= ImVec2(0, 0)*/)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    ImGui::MoveCursor(style.FramePadding.x / 2.0f, style.FramePadding.y / 2.0f);
    ImGui::Dummy(size);
    {
        ImRect rect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        ImRect border_rect = rect;
        border_rect.Expand(1.0f);
        draw_list->AddRect(border_rect.Min, border_rect.Max, 0xFF4455FF);
    }
    ImGui::SameLine();
    return false;
}

//
// # SYSTEM
//

FOUNDATION_STATIC void logo_initialize()
{
    
}

FOUNDATION_STATIC void logo_shutdown()
{

}

DEFINE_SERVICE(LOGO, logo_initialize, logo_shutdown, SERVICE_PRIORITY_UI);
