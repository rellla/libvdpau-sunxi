/*
 * Copyright (c) 2013-2014 Jens Kuske <jenskuske@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifndef __RGBA_H__
#define __RGBA_H__

#include "vdpau_private.h"

VdpStatus rgba_create(rgba_surface_t *rgba,
                      device_ctx_t *device,
                      uint32_t width,
                      uint32_t height,
                      VdpRGBAFormat format);

VdpStatus rgba_prepare(rgba_surface_t *dest,
                       rgba_surface_t *src);

void rgba_destroy(void *rgba);

VdpStatus rgba_put_bits_native(rgba_surface_t *rgba,
                               void const *const *source_data,
                               uint32_t const *source_pitches,
                               VdpRect const *destination_rect);

VdpStatus rgba_put_bits_indexed(rgba_surface_t *rgba,
                                VdpIndexedFormat source_indexed_format,
                                void const *const *source_data,
                                uint32_t const *source_pitch,
                                VdpRect const *destination_rect,
                                VdpColorTableFormat color_table_format,
                                void const *color_table);

VdpStatus rgba_render_output_surface(output_surface_ctx_t *dest,
                              VdpRect const *destination_rect,
                              output_surface_ctx_t *src,
                              VdpRect const *source_rect,
                              VdpColor const *colors,
                              VdpOutputSurfaceRenderBlendState const *blend_state,
                              uint32_t flags);

VdpStatus rgba_render_bitmap_surface(output_surface_ctx_t *dest,
                              VdpRect const *destination_rect,
                              bitmap_surface_ctx_t *src,
                              VdpRect const *source_rect,
                              VdpColor const *colors,
                              VdpOutputSurfaceRenderBlendState const *blend_state,
                              uint32_t flags);

void rgba_clear(rgba_surface_t *rgba);
void rgba_fill(rgba_surface_t *dest, const VdpRect *dest_rect, uint32_t color);
void rgba_blit(rgba_surface_t *dest, const VdpRect *dest_rect, rgba_surface_t *src, const VdpRect *src_rect);

void rgba_flush(rgba_surface_t *rgba);

/* Cache wrapping functions */
void rgba_print_value(void *itemdata);
VdpStatus rgba_create_cache(device_ctx_t *dev);
void rgba_free_cache(CACHE *cache);
void rgba_ref(CACHE *cache, int rgba_hdl);
int rgba_set_recently_rendered(CACHE *cache, int rgba_hdl, rgba_surface_t **rgba);
int rgba_get_refcount(CACHE *cache, int rgba_hdl);
void rgba_unref(CACHE *cache, int rgba_hdl);
void rgba_get_pointer(CACHE *cache, int rgba_hdl, rgba_surface_t **rgba);
int rgba_get_free_surface(device_ctx_t *device, uint32_t width, uint32_t height, VdpRGBAFormat format, rgba_surface_t **rgba);
#endif
