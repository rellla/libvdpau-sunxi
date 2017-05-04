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
// #include "cache.h"

static void cleanup_bitmap_surface(void *ptr, void *meta)
{
	bitmap_surface_ctx_t *surface = ptr;

	rgba_unref(surface->device->cache, surface->rgba_handle);
	sfree(surface->device);
}

VdpStatus vdp_bitmap_surface_create(VdpDevice device,
                                    VdpRGBAFormat rgba_format,
                                    uint32_t width,
                                    uint32_t height,
                                    VdpBool frequently_accessed,
                                    VdpBitmapSurface *surface)
{
	if (!surface)
		return VDP_STATUS_INVALID_POINTER;

	smart device_ctx_t *dev = handle_get(device);
	if (!dev)
		return VDP_STATUS_INVALID_HANDLE;

	smart bitmap_surface_ctx_t *out = handle_alloc(sizeof(*out), cleanup_bitmap_surface);
	if (!out)
		return VDP_STATUS_RESOURCES;

	out->width = width;
	out->height = height;
	out->format = rgba_format;
	out->frequently_accessed = frequently_accessed;
	out->device = sref(dev);

	out->rgba = NULL;
	out->rgba_handle = 0;

	VDPAU_LOG(LDBG, "Create BS width: %d, height: %d", width, height);
	return handle_create(surface, out);
}

VdpStatus vdp_bitmap_surface_get_parameters(VdpBitmapSurface surface,
                                            VdpRGBAFormat *rgba_format,
                                            uint32_t *width,
                                            uint32_t *height,
                                            VdpBool *frequently_accessed)
{
	smart bitmap_surface_ctx_t *out = handle_get(surface);
	if (!out)
		return VDP_STATUS_INVALID_HANDLE;

	if (rgba_format)
		*rgba_format = out->format;

	if (width)
		*width = out->width;

	if (height)
		*height = out->height;

	if (frequently_accessed)
		*frequently_accessed = out->frequently_accessed;

	return VDP_STATUS_OK;
}

VdpStatus vdp_bitmap_surface_put_bits_native(VdpBitmapSurface surface,
                                             void const *const *source_data,
                                             uint32_t const *source_pitches,
                                             VdpRect const *destination_rect)
{
	smart bitmap_surface_ctx_t *out = handle_get(surface);
	if (!out)
		return VDP_STATUS_INVALID_HANDLE;

	if (!out->device->osd_enabled)
		return VDP_STATUS_OK;

	if (destination_rect &&
	    destination_rect->x0 == 0 && destination_rect->x1 == 0 &&
	    destination_rect->y0 == 0 && destination_rect->y1 == 0)
		return VDP_STATUS_OK;

	VdpStatus ret;

	/* We have not yet linked an rgba surface in the bitmap surface, so
	 *   - get a free rgba surface from the cache
	 *   - reference it
	 *   - put the bits on it
	 *   - link the rgba surface and its handle to the bitmap surface
	 */
	if (out->rgba == NULL) {
		ret = rgba_put_bits_native_new(out->device, &out->rgba_handle, &out->rgba,
					       out->width, out->height, out->format,
					       source_data, source_pitches, destination_rect);
	} else {
		/* We have already linked an rgba surface in the bitmap surface,
		 * check, how often it is linked:
		 */
		if (rgba_get_refcount(out->device->cache, out->rgba_handle) == 1) {
			/* 1 time (it already got a put_bits_action, or we did a render action,
			 *         but it is not visible nor queued for displaying yet:
			 *      - simply put the bits on it
			 */
			ret = rgba_put_bits_native_regular(&out->rgba, &out->rgba_handle, out->device,
							   source_data, source_pitches, destination_rect,
							   out->width, out->height, out->format);
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
			ret = rgba_put_bits_native_copy(out->device, &out->rgba_handle, &out->rgba,
						       out->width, out->height, out->format,
						       source_data, source_pitches, destination_rect);
		}
	}

	return ret;
}

VdpStatus vdp_bitmap_surface_query_capabilities(VdpDevice device,
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
