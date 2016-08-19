/*
 * Copyright (c) 2013 Jens Kuske <jenskuske@gmail.com>
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

#include "vdpau_private.h"
#include "rgba.h"
#include "cache.h"

static void cleanup_output_surface(void *ptr, void *meta)
{
	output_surface_ctx_t *surface = ptr;

	rgba_unref(surface->device->cache, surface->rgba_handle);
	pthread_mutex_destroy(&surface->rgba_mutex);
	pthread_mutex_destroy(&surface->mutex);

	if (surface->yuv)
		yuv_unref(surface->yuv);

	sfree(surface->vs);
	sfree(surface->device);
}

VdpStatus vdp_output_surface_create(VdpDevice device,
                                    VdpRGBAFormat rgba_format,
                                    uint32_t width,
                                    uint32_t height,
                                    VdpOutputSurface *surface)
{
	if (!surface)
		return VDP_STATUS_INVALID_POINTER;

	smart device_ctx_t *dev = handle_get(device);
	if (!dev)
		return VDP_STATUS_INVALID_HANDLE;

	smart output_surface_ctx_t *out = handle_alloc(sizeof(*out), cleanup_output_surface);
	if (!out)
		return VDP_STATUS_RESOURCES;

	out->contrast = 1.0;
	out->saturation = 1.0;
	out->width = width;
	out->height = height;
	out->format = rgba_format;
	out->device = sref(dev);

	out->rgba = NULL;
	out->rgba_handle = 0;

	pthread_mutex_init(&out->rgba_mutex, NULL);
	pthread_mutex_init(&out->mutex, NULL);
	VDPAU_LOG(LDBG, "Create OS width: %d, height: %d", width, height);
	return handle_create(surface, out);
}

VdpStatus vdp_output_surface_get_parameters(VdpOutputSurface surface,
                                            VdpRGBAFormat *rgba_format,
                                            uint32_t *width,
                                            uint32_t *height)
{
	smart output_surface_ctx_t *out = handle_get(surface);
	if (!out)
		return VDP_STATUS_INVALID_HANDLE;

	if (rgba_format)
		*rgba_format = out->format;

	if (width)
		*width = out->width;

	if (height)
		*height = out->height;

	return VDP_STATUS_OK;
}

VdpStatus vdp_output_surface_get_bits_native(VdpOutputSurface surface,
                                             VdpRect const *source_rect,
                                             void *const *destination_data,
                                             uint32_t const *destination_pitches)
{
	smart output_surface_ctx_t *out = handle_get(surface);
	if (!out)
		return VDP_STATUS_INVALID_HANDLE;



	return VDP_STATUS_ERROR;
}

VdpStatus vdp_output_surface_put_bits_native(VdpOutputSurface surface,
                                             void const *const *source_data,
                                             uint32_t const *source_pitches,
                                             VdpRect const *destination_rect)
{
	smart output_surface_ctx_t *out = handle_get(surface);
	if (!out)
		return VDP_STATUS_INVALID_HANDLE;

	VdpStatus ret;
	int tmp_hdl;
	rgba_surface_t *tmp_rgba;

	/* We have not yet linked an rgba surface in the output surface, so
	 *   - get a free rgba surface from the cache
	 *   - reference it
	 *   - put the bits on it
	 *   - link the rgba surface and its handle to the bitmap surface
	 */
	if (out->rgba == NULL) {
		tmp_hdl = rgba_get_free_surface(out->device, out->width, out->height, out->format, &tmp_rgba);
		rgba_ref(out->device->cache, tmp_hdl);
		VDPAU_LOG(LDBG, "PBN id: %d on a free surface (with ref=0)", tmp_rgba->id + 1);
		ret = rgba_put_bits_native(tmp_rgba, source_data, source_pitches, destination_rect);
		out->rgba_handle = tmp_hdl;
		rgba_get_pointer(out->device->cache, tmp_hdl, &out->rgba);
	} else {
		/* We have already linked an rgba surface in the bitmap surface,
		 * check, how often it is linked:
		 */
		if (rgba_get_refcount(out->device->cache, out->rgba_handle) == 1) {
		/* 1 time (it already got a put_bits_action, or we did a render action,
		 *         but it is not visible nor queued for displaying yet:
		 *      - simply put the bits on it
		 */
			VDPAU_LOG(LDBG, "PBN id: %d on a same surface (with ref=1)", out->rgba->id + 1);
			ret = rgba_put_bits_native(out->rgba, source_data, source_pitches, destination_rect);
		} else {
		/* >= 2 times (it already got a put_bits_action, AND it's possible,
		 *             that it is visible or queued for displaying yet,
		 *             so we must not touch that surface!
		 *      - get a free rgba surface from the cache
		 *      - duplicate the originally intended dest surface into the new one
		 *      - put the bits on the new surface
		 *      - reference the new surface
		 *      - unreference the surface, we should originally put the bits into
		 *      - link the rgba surface and its handle to the bitmap surface
		 */
			tmp_hdl = rgba_get_free_surface(out->device, out->width, out->height, out->format, &tmp_rgba);
			rgba_prepare(tmp_rgba, out->rgba);

			VDPAU_LOG(LDBG, "PBN id: %d on a new surface and copy the old into it", tmp_rgba->id + 1);
			ret = rgba_put_bits_native(tmp_rgba, source_data, source_pitches, destination_rect);
			rgba_unref(out->device->cache, out->rgba_handle);
			rgba_ref(out->device->cache, tmp_hdl);
			out->rgba_handle = tmp_hdl;
			rgba_get_pointer(out->device->cache, tmp_hdl, &out->rgba);
		}
	}

	return ret;
}

VdpStatus vdp_output_surface_put_bits_indexed(VdpOutputSurface surface,
                                              VdpIndexedFormat source_indexed_format,
                                              void const *const *source_data,
                                              uint32_t const *source_pitch,
                                              VdpRect const *destination_rect,
                                              VdpColorTableFormat color_table_format,
                                              void const *color_table)
{
	smart output_surface_ctx_t *out = handle_get(surface);
	if (!out)
		return VDP_STATUS_INVALID_HANDLE;

	VdpStatus ret;
	int tmp_hdl;
	rgba_surface_t *tmp_rgba;

	/* We have not yet linked an rgba surface in the output surface, so
	 *   - get a free rgba surface from the cache
	 *   - reference it
	 *   - put the bits on it
	 *   - link the rgba surface and its handle to the bitmap surface
	 */
	if (out->rgba == NULL) {
		tmp_hdl = rgba_get_free_surface(out->device, out->width, out->height, out->format, &tmp_rgba);
		rgba_ref(out->device->cache, tmp_hdl);
		VDPAU_LOG(LDBG, "PBI id: %d on a free surface (with ref=0)", tmp_rgba->id + 1);
		ret = rgba_put_bits_indexed(tmp_rgba, source_indexed_format, source_data, source_pitch,
					    destination_rect, color_table_format, color_table);
		out->rgba_handle = tmp_hdl;
		rgba_get_pointer(out->device->cache, tmp_hdl, &out->rgba);
	} else {
		/* We have already linked an rgba surface in the bitmap surface,
		 * check, how often it is linked:
		 */
		if (rgba_get_refcount(out->device->cache, out->rgba_handle) == 1) {
		/* 1 time (it already got a put_bits_action, or we did a render action,
		 *         but it is not visible nor queued for displaying yet:
		 *      - simply put the bits on it
		 */
			VDPAU_LOG(LDBG, "PBI id: %d on a same surface (with ref=1)", out->rgba->id + 1);
			ret = rgba_put_bits_indexed(out->rgba, source_indexed_format, source_data, source_pitch,
					            destination_rect, color_table_format, color_table);
		} else {
		/* >= 2 times (it already got a put_bits_action, AND it's possible,
		 *             that it is visible or queued for displaying yet,
		 *             so we must not touch that surface!
		 *      - get a free rgba surface from the cache
		 *      - duplicate the originally intended dest surface into the new one
		 *      - put the bits on the new surface
		 *      - reference the new surface
		 *      - unreference the surface, we should originally put the bits into
		 *      - link the rgba surface and its handle to the bitmap surface
		 */
			tmp_hdl = rgba_get_free_surface(out->device, out->width, out->height, out->format, &tmp_rgba);
			rgba_prepare(tmp_rgba, out->rgba);

			VDPAU_LOG(LDBG, "PBI id: %d on a new surface and copy the old into it", tmp_rgba->id + 1);
			ret = rgba_put_bits_indexed(tmp_rgba, source_indexed_format, source_data, source_pitch,
					            destination_rect, color_table_format, color_table);
			rgba_unref(out->device->cache, out->rgba_handle);
			rgba_ref(out->device->cache, tmp_hdl);
			out->rgba_handle = tmp_hdl;
			rgba_get_pointer(out->device->cache, tmp_hdl, &out->rgba);
		}
	}

	return ret;
}

VdpStatus vdp_output_surface_put_bits_y_cb_cr(VdpOutputSurface surface,
                                              VdpYCbCrFormat source_ycbcr_format,
                                              void const *const *source_data,
                                              uint32_t const *source_pitches,
                                              VdpRect const *destination_rect,
                                              VdpCSCMatrix const *csc_matrix)
{
	smart output_surface_ctx_t *out = handle_get(surface);
	if (!out)
		return VDP_STATUS_INVALID_HANDLE;

	return VDP_STATUS_ERROR;
}

VdpStatus vdp_output_surface_render_output_surface(VdpOutputSurface destination_surface,
                                                   VdpRect const *destination_rect,
                                                   VdpOutputSurface source_surface,
                                                   VdpRect const *source_rect,
                                                   VdpColor const *colors,
                                                   VdpOutputSurfaceRenderBlendState const *blend_state,
                                                   uint32_t flags)
{
	smart output_surface_ctx_t *out = handle_get(destination_surface);
	if (!out)
		return VDP_STATUS_INVALID_HANDLE;

	smart output_surface_ctx_t *in = handle_get(source_surface);

	return rgba_render_output_surface(out, destination_rect, in, source_rect,
					colors, blend_state, flags);
}

VdpStatus vdp_output_surface_render_bitmap_surface(VdpOutputSurface destination_surface,
                                                   VdpRect const *destination_rect,
                                                   VdpBitmapSurface source_surface,
                                                   VdpRect const *source_rect,
                                                   VdpColor const *colors,
                                                   VdpOutputSurfaceRenderBlendState const *blend_state,
                                                   uint32_t flags)
{
	smart output_surface_ctx_t *out = handle_get(destination_surface);
	if (!out)
		return VDP_STATUS_INVALID_HANDLE;

	smart bitmap_surface_ctx_t *in = handle_get(source_surface);

	return rgba_render_bitmap_surface(out, destination_rect, in, source_rect,
					colors, blend_state, flags);
}

VdpStatus vdp_output_surface_query_capabilities(VdpDevice device,
                                                VdpRGBAFormat surface_rgba_format,
                                                VdpBool *is_supported,
                                                uint32_t *max_width,
                                                uint32_t *max_height)
{
	if (!is_supported || !max_width || !max_height)
		return VDP_STATUS_INVALID_POINTER;

	smart device_ctx_t *dev = handle_get(device);
	if (!dev)
		return VDP_STATUS_INVALID_HANDLE;

	*is_supported = (surface_rgba_format == VDP_RGBA_FORMAT_R8G8B8A8 || surface_rgba_format == VDP_RGBA_FORMAT_B8G8R8A8);
	*max_width = 8192;
	*max_height = 8192;

	return VDP_STATUS_OK;
}

VdpStatus vdp_output_surface_query_get_put_bits_native_capabilities(VdpDevice device,
                                                                    VdpRGBAFormat surface_rgba_format,
                                                                    VdpBool *is_supported)
{
	if (!is_supported)
		return VDP_STATUS_INVALID_POINTER;

	smart device_ctx_t *dev = handle_get(device);
	if (!dev)
		return VDP_STATUS_INVALID_HANDLE;

	*is_supported = VDP_FALSE;

	return VDP_STATUS_OK;
}

VdpStatus vdp_output_surface_query_put_bits_indexed_capabilities(VdpDevice device,
                                                                 VdpRGBAFormat surface_rgba_format,
                                                                 VdpIndexedFormat bits_indexed_format,
                                                                 VdpColorTableFormat color_table_format,
                                                                 VdpBool *is_supported)
{
	if (!is_supported)
		return VDP_STATUS_INVALID_POINTER;

	smart device_ctx_t *dev = handle_get(device);
	if (!dev)
		return VDP_STATUS_INVALID_HANDLE;

	*is_supported = VDP_FALSE;

	return VDP_STATUS_OK;
}

VdpStatus vdp_output_surface_query_put_bits_y_cb_cr_capabilities(VdpDevice device,
                                                                 VdpRGBAFormat surface_rgba_format,
                                                                 VdpYCbCrFormat bits_ycbcr_format,
                                                                 VdpBool *is_supported)
{
	if (!is_supported)
		return VDP_STATUS_INVALID_POINTER;

	smart device_ctx_t *dev = handle_get(device);
	if (!dev)
		return VDP_STATUS_INVALID_HANDLE;

	*is_supported = VDP_FALSE;

	return VDP_STATUS_OK;
}
