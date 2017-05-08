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
/*
 * Rgba cache wrapper functions
 */
VdpStatus rgba_create_cache(device_ctx_t *dev);

void rgba_free_cache(CACHE *cache);

void rgba_ref(CACHE *cache, int rgba_hdl);

void rgba_unref(CACHE *cache, int rgba_hdl);

int rgba_get_refcount(CACHE *cache, int rgba_hdl);

void rgba_print_value(void *itemdata);
int rgba_compare_params(void *param, void *rgba);
int rgba_create_surface(device_ctx_t *device,
                        uint32_t width,
                        uint32_t height,
                        VdpRGBAFormat format,
                        rgba_surface_t **rgba);

/*
 * Rgba helper functions
 */
void rgba_clear(rgba_surface_t *rgba);
void rgba_flush(rgba_surface_t *rgba);

VdpStatus rgba_put_bits_native_new(device_ctx_t *device,
                                   int *rgba_handle,
                                   rgba_surface_t **rgba,
                                   uint32_t width,
                                   uint32_t height,
                                   VdpRGBAFormat format,
                                   void const *const *source_data,
                                   uint32_t const *source_pitches,
                                   VdpRect const *destination_rect);

VdpStatus rgba_put_bits_native_copy(device_ctx_t *device,
                                    int *rgba_handle,
                                    rgba_surface_t **rgba,
                                    uint32_t width,
                                    uint32_t height,
                                    VdpRGBAFormat format,
                                    void const *const *source_data,
                                    uint32_t const *source_pitches,
                                    VdpRect const *destination_rect);

VdpStatus rgba_put_bits_native_regular(rgba_surface_t **rgba,
                                       int *rgba_handle,
                                       device_ctx_t *device,
                                       void const *const *source_data,
                                       uint32_t const *source_pitches,
                                       VdpRect const *destination_rect,
                                       uint32_t width,
                                       uint32_t height,
                                       VdpRGBAFormat format);

VdpStatus rgba_put_bits_indexed_new(device_ctx_t *device,
                                    int *rgba_handle,
                                    rgba_surface_t **rgba, 
                                    uint32_t width,
                                    uint32_t height,
                                    VdpRGBAFormat format,
                                    VdpIndexedFormat source_indexed_format,
                                    void const *const *source_data,
                                    uint32_t const *source_pitch,
                                    VdpRect const *destination_rect,
                                    VdpColorTableFormat color_table_format,
                                    void const *color_table);

VdpStatus rgba_put_bits_indexed_copy(device_ctx_t *device,
                                     int *rgba_handle,
                                     rgba_surface_t **rgba,
                                     uint32_t width,
                                     uint32_t height,
                                     VdpRGBAFormat format,
                                     VdpIndexedFormat source_indexed_format,
                                     void const *const *source_data,
                                     uint32_t const *source_pitch,
                                     VdpRect const *destination_rect,
                                     VdpColorTableFormat color_table_format,
                                     void const *color_table);

VdpStatus rgba_put_bits_indexed_regular(rgba_surface_t **rgba,
                                        int *rgba_handle,
                                        device_ctx_t *device,
                                        VdpIndexedFormat source_indexed_format,
                                        void const *const *source_data,
                                        uint32_t const *source_pitch,
                                        VdpRect const *destination_rect,
                                        VdpColorTableFormat color_table_format,
                                        void const *color_table,
                                        uint32_t width,
                                        uint32_t height,
                                        VdpRGBAFormat format);

VdpStatus rgba_render_surface(rgba_surface_t **dest,
                                     int *dest_hdl,
                                     VdpRect const *destination_rect,
                                     rgba_surface_t *src,
                                     int src_hdl,
                                     VdpRect const *source_rect,
                                     VdpColor const *colors,
                                     VdpOutputSurfaceRenderBlendState const *blend_state,
                                     uint32_t flags,
                                     uint32_t width,
                                     uint32_t height,
                                     VdpRGBAFormat format,
                                     device_ctx_t *device);
#endif
