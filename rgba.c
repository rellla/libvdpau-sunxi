/*
 * Copyright (c) 2013-2014 Jens Kuske <jenskuske@gmail.com>
 * Copyright (c) 2016-2017 Andreas Baierl <ichgeh@imkreisrum.de>
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

#include <string.h>
#include <cedrus/cedrus.h>
#include <sys/ioctl.h>
#include "vdpau_private.h"
#include "rgba.h"
#include "rgba_pixman.h"
#include "rgba_g2d.h"
#include "cache.h"

/*
 * Rgba helper functions
 */
static VdpStatus rgba_create(rgba_surface_t *rgba,
                      device_ctx_t *device,
                      uint32_t width,
                      uint32_t height,
                      VdpRGBAFormat format)
{
	if (format != VDP_RGBA_FORMAT_B8G8R8A8 && format != VDP_RGBA_FORMAT_R8G8B8A8)
		return VDP_STATUS_INVALID_RGBA_FORMAT;

	if (width < 1 || width > 8192 || height < 1 || height > 8192)
		return VDP_STATUS_INVALID_SIZE;

	rgba->device = sref(device);
	rgba->width = width;
	rgba->height = height;
	rgba->format = format;

	if (device->osd_enabled)
	{
		rgba->dirty.x0 = width;
		rgba->dirty.y0 = height;
		rgba->dirty.x1 = 0;
		rgba->dirty.y1 = 0;
		rgba->id = 0;
	}

	return VDP_STATUS_OK;
}

static void rgba_destroy(void *rgba_p)
{
	rgba_surface_t *rgba = rgba_p;

	if (!rgba || !rgba->device)
		return;

	if (rgba->device->osd_enabled && rgba->data)
	{
		if (!rgba->device->g2d_enabled)
			vdp_pixman_unref(rgba);
		cedrus_mem_free(rgba->data);
		rgba->data = NULL;
	}

	sfree(rgba->device);
}

void rgba_flush(rgba_surface_t *rgba)
{
	if (rgba->flags & RGBA_FLAG_NEEDS_FLUSH)
	{
		VDPAU_LOG(LDBG2, "rgba flush (%d)", rgba->id);
		cedrus_mem_flush_cache(rgba->data);
		rgba->flags &= ~RGBA_FLAG_NEEDS_FLUSH;
	}
}

static void rgba_fill(rgba_surface_t *dest, const VdpRect *dest_rect, uint32_t color)
{
	if (dest->device->osd_enabled)
	{
		if(dest->device->g2d_enabled)
		{
			rgba_flush(dest);
			g2d_fill(dest, dest_rect, color);
		}
		else
		{
			vdp_pixman_fill(dest, dest_rect, color);
			dest->flags |= RGBA_FLAG_NEEDS_FLUSH;
		}
	}
}

static void rgba_blit(rgba_surface_t *dest, const VdpRect *dest_rect, rgba_surface_t *src, const VdpRect *src_rect)
{
	if (dest->device->osd_enabled)
	{
		if(dest->device->g2d_enabled)
		{
			rgba_flush(dest);
			rgba_flush(src);
			g2d_blit(dest, dest_rect, src, src_rect);
		}
		else
		{
			vdp_pixman_blit(dest, dest_rect, src, src_rect);
			dest->flags |= RGBA_FLAG_NEEDS_FLUSH;
		}
	}
}

static void rgba_clear(rgba_surface_t *rgba)
{
	if (!(rgba->flags & RGBA_FLAG_DIRTY))
		return;

	rgba_fill(rgba, &rgba->dirty, 0x00000000);
	rgba->flags &= ~RGBA_FLAG_DIRTY;
	rgba->dirty.x0 = rgba->width;
	rgba->dirty.y0 = rgba->height;
	rgba->dirty.x1 = 0;
	rgba->dirty.y1 = 0;
	VDPAU_LOG(LDBG2, "rgba clear (%d)", rgba->id);
}

static void rgba_duplicate_attribs(rgba_surface_t *dest, rgba_surface_t *src)
{
	/* duplicate attributes */
	dest->old_id = src->old_id;
	dest->id = src->id;
	dest->flags = src->flags;
	dest->format = src->format;
	dest->width = src->width;
	dest->height = src->height;
	dest->d_rect.x0 = src->d_rect.x0;
	dest->d_rect.y0 = src->d_rect.y0;
	dest->d_rect.x1 = src->d_rect.x1;
	dest->d_rect.y1 = src->d_rect.y1;
	dest->s_rect.x0 = src->s_rect.x0;
	dest->s_rect.x1 = src->s_rect.x1;
	dest->s_rect.y0 = src->s_rect.y0;
	dest->s_rect.y1 = src->s_rect.y1;
	dest->dirty.x0 = src->dirty.x0;
	dest->dirty.x1 = src->dirty.x1;
	dest->dirty.y0 = src->dirty.y0;
	dest->dirty.y1 = src->dirty.y1;
}

static VdpStatus rgba_prepare(rgba_surface_t *dest, rgba_surface_t *src)
{
	if (dest == NULL)
		return VDP_STATUS_RESOURCES;

	if (dest->data == NULL)
	{
		dest->data = cedrus_mem_alloc(dest->device->cedrus, dest->width * dest->height * 4);
		if (!dest->data)
			return VDP_STATUS_RESOURCES;

		if(!dest->device->g2d_enabled)
			vdp_pixman_ref(dest);

		rgba_fill(dest, NULL, 0x00000000);
	}

	if (src != NULL)
	{
		 /* copy full width dirty area */
		if (src->dirty.x0 == 0 && src->dirty.x1 == src->width)
			memcpy(cedrus_mem_get_pointer(dest->data) + src->dirty.y0 * src->width * 4,
			       cedrus_mem_get_pointer(src->data) + src->dirty.y0 * src->width * 4,
			       (src->dirty.x1 - src->dirty.x0) * (src->dirty.y1 - src->dirty.y0) * 4);
		else {
		/* copy full dirty rect */
			unsigned int y;
			for (y = src->dirty.y0; y < src->dirty.y1; y ++)
				memcpy(cedrus_mem_get_pointer(dest->data) + (y * src->width + src->dirty.x0) * 4,
				       cedrus_mem_get_pointer(src->data)  + (y * src->width + src->dirty.x0) * 4,
				       src->dirty.x1 - src->dirty.x0);
		}

		rgba_duplicate_attribs(dest, src);
	}

	return VDP_STATUS_OK;
}

static int rect_changed(VdpRect const *rect1, VdpRect rect2)
{
	if (!rect1)
		return 1;

	if (rect1->x0 == rect2.x0 &&
	    rect1->y0 == rect2.y0 &&
	    rect1->x1 == rect2.x1 &&
	    rect1->y1 == rect2.y1)
		return 0;

	VDPAU_LOG(LDBG2, "Rect changed: %d,%d|%d,%d - %d,%d|%d,%d",
		  rect1->x0, rect1->y0, rect1->x1, rect1->y1,
		  rect2.x0, rect2.y0, rect2.x1, rect2.y1);

	return 1;
}

static int rgba_changed(rgba_surface_t *dest,
			rgba_surface_t *src,
			VdpRect const *d_rect,
			VdpRect const *s_rect)
{
	if (!dest && !src)
		return 0;

	int id = -1;
	int old_id = -1;
	if (src)
	{
		id = src->id;
		old_id = src->old_id;
	}

	if ((!dest && src) ||
	    (dest && !src) ||
	    (dest->id != id) ||
	    (dest->old_id != old_id) ||
	    rect_changed(d_rect, dest->d_rect) ||
	    rect_changed(s_rect, dest->s_rect))
		return 1;

	return 0;
}

static void dirty_add_rect(VdpRect *dirty, const VdpRect *rect)
{
	dirty->x0 = min(dirty->x0, rect->x0);
	dirty->y0 = min(dirty->y0, rect->y0);
	dirty->x1 = max(dirty->x1, rect->x1);
	dirty->y1 = max(dirty->y1, rect->y1);
}

/*
 * Rgba cache wrapper functions
 */
VdpStatus rgba_create_cache(device_ctx_t *dev)
{
	CACHE *disp_cache = cache_create();
	if (!disp_cache)
		return VDP_STATUS_RESOURCES;

	dev->cache = disp_cache;

	return VDP_STATUS_OK;
}

void rgba_free_cache(CACHE *cache)
{
	cache_free(cache, rgba_destroy);
}

void rgba_ref(CACHE *cache, int rgba_hdl)
{
	cache_hdl_ref(rgba_hdl, cache);
}

int rgba_get_refcount(CACHE *cache, int rgba_hdl)
{
	return cache_hdl_get_ref(rgba_hdl, cache);
}

static void rgba_get_pointer(CACHE *cache, int rgba_hdl, rgba_surface_t **rgba)
{
	cache_get_pointer(rgba_hdl, cache, (void *)rgba);
}

void rgba_unref(CACHE *cache, int rgba_hdl)
{
	rgba_surface_t *rgba;
	rgba_get_pointer(cache, rgba_hdl, &rgba);

	if (cache_hdl_get_ref(rgba_hdl, cache) == 1)
		rgba_clear(rgba);

	cache_hdl_unref(rgba_hdl, cache, rgba_destroy);
}

static int rgba_get_recently_rendered(CACHE *cache, rgba_surface_t **rgba)
{
	return cache_get_head(cache, (void *)rgba);
}

static int rgba_set_recently_rendered(CACHE *cache, int rgba_hdl, rgba_surface_t **rgba)
{
	cache_set_head(cache, rgba_hdl);
	return cache_get_head(cache, (void *)rgba);
}

static int rgba_hdl_create(CACHE *cache, rgba_surface_t *rgba)
{
	return cache_hdl_create(cache, (void *)rgba);
}

static int rgba_get_free_surface(device_ctx_t *device,
                          uint32_t width,
                          uint32_t height,
                          VdpRGBAFormat format,
                          rgba_surface_t **rgba)
{
	int tmp_hdl;
	rgba_surface_t *tmp_rgba;

	tmp_hdl = cache_hdl_get(device->cache, (void *)rgba);
	if (!tmp_hdl)
	{
		VDPAU_LOG(LDBG2, "create new rgba");
		tmp_rgba = (rgba_surface_t *)calloc(1, sizeof(rgba_surface_t));
		rgba_create(tmp_rgba, device, width, height, format);
		rgba_prepare(tmp_rgba, NULL);
		tmp_hdl = rgba_hdl_create(device->cache, tmp_rgba);
		*rgba = tmp_rgba;
	}

	return tmp_hdl;
}

#ifdef CACHE_DEBUG
void rgba_print_value(void *rgba)
{
	printf(">>> ID %d DATA %x RGBA %x dirty? %d" ,
		((rgba_surface_t *)rgba)->id,
		(unsigned int)((rgba_surface_t *)rgba)->data,
		(unsigned int)((rgba_surface_t *)rgba),
		(((rgba_surface_t *)rgba)->flags >> 0) & 1);
}
#endif

/*
 * VDPAU API rgba helper functions
 */
static VdpStatus rgba_put_bits_native(rgba_surface_t *rgba,
                                      void const *const *source_data,
                                      uint32_t const *source_pitches,
                                      VdpRect const *destination_rect)
{
	VdpStatus ret;
	ret = rgba_prepare(rgba, NULL);
	if (ret != VDP_STATUS_OK)
		return ret;

	VdpRect d_rect = {0, 0, rgba->width, rgba->height};
	if (destination_rect)
		d_rect = *destination_rect;

	/* Skip the copy, if d_rect is zerosized. mpv does this with it's preemtion implementation */
	if (d_rect.x0 == 0 &&
	    d_rect.x1 == 0 &&
	    d_rect.y0 == 0 &&
	    d_rect.y1 == 0)
		return VDP_STATUS_OK;

	if (0 == d_rect.x0 && rgba->width == d_rect.x1 && source_pitches[0] == d_rect.x1 * 4) {
		// full width
		const int bytes_to_copy =
			(d_rect.x1 - d_rect.x0) * (d_rect.y1 - d_rect.y0) * 4;
		memcpy(cedrus_mem_get_pointer(rgba->data) + d_rect.y0 * rgba->width * 4,
			   source_data[0], bytes_to_copy);
	} else {
		const unsigned int bytes_in_line = (d_rect.x1-d_rect.x0) * 4;
		unsigned int y;
		for (y = d_rect.y0; y < d_rect.y1; y ++) {
			memcpy(cedrus_mem_get_pointer(rgba->data) + (y * rgba->width + d_rect.x0) * 4,
				   source_data[0] + (y - d_rect.y0) * source_pitches[0],
				   bytes_in_line);
		}
	}

	rgba->flags |= RGBA_FLAG_DIRTY | RGBA_FLAG_NEEDS_FLUSH;
	dirty_add_rect(&rgba->dirty, &d_rect);

	rgba->id++;

	return VDP_STATUS_OK;
}

VdpStatus rgba_put_bits_native_new(device_ctx_t *device,
                                   int *rgba_handle,
                                   rgba_surface_t **rgba,
                                   uint32_t width,
                                   uint32_t height,
                                   VdpRGBAFormat format,
                                   void const *const *source_data,
                                   uint32_t const *source_pitches,
                                   VdpRect const *destination_rect)
{
	VdpStatus ret;

	int tmp_handle;
	rgba_surface_t *tmp_rgba;

	tmp_handle = rgba_get_free_surface(device, width, height, format, &tmp_rgba);
	rgba_ref(device->cache, tmp_handle);
	ret = rgba_put_bits_native(tmp_rgba, source_data, source_pitches, destination_rect);
	*rgba_handle = tmp_handle;
	rgba_get_pointer(device->cache, tmp_handle, rgba);

	VDPAU_LOG(LDBG, "PBN id: %d on a new surface (with ref=0)", (*rgba)->id + 1);

	return ret;
}

VdpStatus rgba_put_bits_native_copy(device_ctx_t *device,
                                    int *rgba_handle,
                                    rgba_surface_t **rgba,
                                    uint32_t width,
                                    uint32_t height,
                                    VdpRGBAFormat format,
                                    void const *const *source_data,
                                    uint32_t const *source_pitches,
                                    VdpRect const *destination_rect)
{
	VdpStatus ret;

	int tmp_handle;
	rgba_surface_t *tmp_rgba;

	tmp_handle = rgba_get_free_surface(device, width, height, format, &tmp_rgba);
	rgba_prepare(tmp_rgba, *rgba);
	ret = rgba_put_bits_native(tmp_rgba, source_data, source_pitches, destination_rect);
	rgba_unref(device->cache, *rgba_handle);
	rgba_ref(device->cache, tmp_handle);
	*rgba_handle = tmp_handle;
	rgba_get_pointer(device->cache, tmp_handle, rgba);

	VDPAU_LOG(LDBG, "PBN id: %d on a new surface and copy the old into it", (*rgba)->id + 1);

	return ret;
}

VdpStatus rgba_put_bits_native_regular(rgba_surface_t *rgba,
                                       void const *const *source_data,
                                       uint32_t const *source_pitches,
                                       VdpRect const *destination_rect)
{
	VdpStatus ret;

	ret = rgba_put_bits_native(rgba, source_data, source_pitches, destination_rect);

	VDPAU_LOG(LDBG, "PBN id: %d on a same surface (with ref=1)", rgba->id + 1);

	return ret;
}

static VdpStatus rgba_put_bits_indexed(rgba_surface_t *rgba,
                                       VdpIndexedFormat source_indexed_format,
                                       void const *const *source_data,
                                       uint32_t const *source_pitch,
                                       VdpRect const *destination_rect,
                                       VdpColorTableFormat color_table_format,
                                       void const *color_table)
{
	if (color_table_format != VDP_COLOR_TABLE_FORMAT_B8G8R8X8)
		return VDP_STATUS_INVALID_COLOR_TABLE_FORMAT;

	VdpStatus ret;
	ret = rgba_prepare(rgba, NULL);
	if (ret != VDP_STATUS_OK)
		return ret;

	int x, y;
	const uint32_t *colormap = color_table;
	const uint8_t *src_ptr = source_data[0];
	uint32_t *dst_ptr = cedrus_mem_get_pointer(rgba->data);

	VdpRect d_rect = {0, 0, rgba->width, rgba->height};
	if (destination_rect)
		d_rect = *destination_rect;

	/* Skip the copy, if d_rect is zerosized. mpv does this with it's preemtion implementation */
	if (d_rect.x0 == 0 &&
	    d_rect.x1 == 0 &&
	    d_rect.y0 == 0 &&
	    d_rect.y1 == 0)
		return VDP_STATUS_OK;

	dst_ptr += d_rect.y0 * rgba->width;
	dst_ptr += d_rect.x0;

	for (y = 0; y < d_rect.y1 - d_rect.y0; y++)
	{
		for (x = 0; x < d_rect.x1 - d_rect.x0; x++)
		{
			uint8_t i, a;
			switch (source_indexed_format)
			{
			case VDP_INDEXED_FORMAT_I8A8:
				i = src_ptr[x * 2];
				a = src_ptr[x * 2 + 1];
				break;
			case VDP_INDEXED_FORMAT_A8I8:
				a = src_ptr[x * 2];
				i = src_ptr[x * 2 + 1];
				break;
			default:
				return VDP_STATUS_INVALID_INDEXED_FORMAT;
			}
			dst_ptr[x] = (colormap[i] & 0x00ffffff) | (a << 24);
		}
		src_ptr += source_pitch[0];
		dst_ptr += rgba->width;
	}

	rgba->flags |= RGBA_FLAG_DIRTY | RGBA_FLAG_NEEDS_FLUSH;
	dirty_add_rect(&rgba->dirty, &d_rect);

	rgba->id++;

	return VDP_STATUS_OK;
}

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
                                    void const *color_table)
{
	VdpStatus ret;

	int tmp_handle;
	rgba_surface_t *tmp_rgba;

	tmp_handle = rgba_get_free_surface(device, width, height, format, &tmp_rgba);
	rgba_ref(device->cache, tmp_handle);
	ret = rgba_put_bits_indexed(tmp_rgba, source_indexed_format, source_data, source_pitch,
				    destination_rect, color_table_format, color_table);
	*rgba_handle = tmp_handle;
	rgba_get_pointer(device->cache, tmp_handle, rgba);

	VDPAU_LOG(LDBG, "PBN id: %d on a new surface (with ref=0)", (*rgba)->id + 1);

	return ret;
}

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
                                     void const *color_table)
{
	VdpStatus ret;

	int tmp_handle;
	rgba_surface_t *tmp_rgba;

	tmp_handle = rgba_get_free_surface(device, width, height, format, &tmp_rgba);
	rgba_prepare(tmp_rgba, *rgba);
	ret = rgba_put_bits_indexed(tmp_rgba, source_indexed_format, source_data, source_pitch,
			            destination_rect, color_table_format, color_table);
	rgba_unref(device->cache, *rgba_handle);
	rgba_ref(device->cache, tmp_handle);
	*rgba_handle = tmp_handle;
	rgba_get_pointer(device->cache, tmp_handle, rgba);

	VDPAU_LOG(LDBG, "PBI id: %d on a new surface and copy the old into it", tmp_rgba->id + 1);

	return ret;
}

VdpStatus rgba_put_bits_indexed_regular(rgba_surface_t *rgba,
                                        VdpIndexedFormat source_indexed_format,
                                        void const *const *source_data,
                                        uint32_t const *source_pitch,
                                        VdpRect const *destination_rect,
                                        VdpColorTableFormat color_table_format,
                                        void const *color_table)
{
	VdpStatus ret;

	ret = rgba_put_bits_indexed(rgba, source_indexed_format, source_data, source_pitch,
              destination_rect, color_table_format, color_table);

	VDPAU_LOG(LDBG, "PBI id: %d on a same surface (with ref=1)", rgba->id + 1);

	return ret;
}

static VdpStatus rgba_do_render(rgba_surface_t *dest,
                                VdpRect *d_rect,
                                rgba_surface_t *src,
                                VdpRect *s_rect)
{
	if (!src)
	{
		rgba_fill(dest, d_rect, 0xffffffff);
		VDPAU_LOG(LDBG2, "Fill");

		/* save the rect, to remember the fill */
		dest->d_rect.x0 = d_rect->x0;
		dest->d_rect.y0 = d_rect->y0;
		dest->d_rect.x1 = d_rect->x1;
		dest->d_rect.y1 = d_rect->y1;
	}
	else
	{
		rgba_blit(dest, d_rect, src, s_rect);
		VDPAU_LOG(LDBG2, "Blit src ID = %d", src->id);

		/* rotate ids and save the rect, to remember the blit */
		src->old_id = dest->id;
		dest->old_id = dest->id;
		dest->id = src->id;
		dest->d_rect.x0 = d_rect->x0;
		dest->d_rect.y0 = d_rect->y0;
		dest->d_rect.x1 = d_rect->x1;
		dest->d_rect.y1 = d_rect->y1;
		dest->s_rect.x0 = s_rect->x0;
		dest->s_rect.x1 = s_rect->x1;
		dest->s_rect.y0 = s_rect->y0;
		dest->s_rect.y1 = s_rect->y1;
	}

	dirty_add_rect(&dest->dirty, d_rect);

	return VDP_STATUS_OK;
}

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
                                     device_ctx_t *device,
                                     pthread_mutex_t mutex)
{
	if (colors || flags)
		VDPAU_LOG(LWARN, "%s: colors and flags not implemented!", __func__);

	rgba_surface_t *tmp_rgba = NULL;
	int tmp_hdl = 0;


	/* We never rendered something into the dest surface,
	   so just link and reference the source surface */
	if ((*dest == NULL) || (dest_hdl == 0))
	{
		pthread_mutex_lock(&mutex);
		/* Set pointer to the src surface and reference it */
		if (src_hdl != 0)
		{
			*dest_hdl = rgba_set_recently_rendered(device->cache, src_hdl, dest);
			VDPAU_LOG(LDBG2, "RBS: no rgba surface yet, so just link and reference it");
			rgba_ref(device->cache, *dest_hdl);
			(*dest)->flags |= RGBA_FLAG_DIRTY;
		}
		/* We have not even a src surface, so
		 *   - get a free surface
		 *   - render to it with src=NULL, 
		 *     which makes a rgba_fill to the complete rect with all colors treated to 1.0
		 *   - reference the new surface
		 *   - set it as the last recently rendered one
		 *
		 *   This is a special case, which should be avoided.
		 */
		else
		{
			tmp_hdl = rgba_get_free_surface(device, (*dest)->width, (*dest)->height, (*dest)->format, &tmp_rgba);
			rgba_prepare(tmp_rgba, NULL);

			VDPAU_LOG(LDBG, "RBS: render nothing on a new surface!!!");
			VdpRect tmp_s_rect = {0, 0, 0, 0};
			VdpRect tmp_d_rect = {0, 0, width, height};
			rgba_do_render(tmp_rgba, &tmp_d_rect, src, &tmp_s_rect);
			rgba_ref(device->cache, tmp_hdl);
			(*dest)->flags |= RGBA_FLAG_DIRTY;
			*dest_hdl = rgba_set_recently_rendered(device->cache, tmp_hdl, dest);
		}
		pthread_mutex_unlock(&mutex);

		return VDP_STATUS_OK;
	}

	// set up source/destination rects using defaults where required
	VdpRect s_rect = {0, 0, 0, 0};
	VdpRect d_rect = {0, 0, (*dest)->width, (*dest)->height};
	s_rect.x1 = src ? src->width : 0;
	s_rect.y1 = src ? src->height : 0;

	if (source_rect)
		s_rect = *source_rect;
	if (destination_rect)
		d_rect = *destination_rect;

	// ignore zero-sized surfaces (also workaround for g2d driver bug)
	if (s_rect.x0 == s_rect.x1 || s_rect.y0 == s_rect.y1 ||
	    d_rect.x0 == d_rect.x1 || d_rect.y0 == d_rect.y1)
		return VDP_STATUS_OK;

	pthread_mutex_lock(&mutex);

	/* Get the surface, which was last recently rendered */
	tmp_hdl = rgba_get_recently_rendered(device->cache, &tmp_rgba);

	/* We have no different rendering action than the last time, so
	 *   - unref the currently linked surface (in case we have one)
	 *   - append the last recently rendered rgba to the surface and reference it
	 */
	if (!rgba_changed(tmp_rgba, src, &d_rect, &s_rect))
	{
		if (rgba_get_refcount(device->cache, *dest_hdl) > 0)
			rgba_unref(device->cache, *dest_hdl);

		*dest_hdl = rgba_set_recently_rendered(device->cache, tmp_hdl, dest);
		rgba_ref(device->cache, *dest_hdl);
		(*dest)->flags |= RGBA_FLAG_DIRTY;

		pthread_mutex_unlock(&mutex);

		return VDP_STATUS_OK;
	}

	/* We have a different rendering action than the last time and
	 * the current rgba is referenced from any surface:
	 *    - get a free rgba from the cache
	 *    - copy the current dest rgba (dirty area and attributes)
	 *      into that new rgba (via memcpy)
	 *    - render the src surface into that new rgba
	 *    - unreference the currently linked rgba (dest rgba)
	 *    - append the new rgba to the surface and reference it
	 */
	VDPAU_LOG(LDBG, "RBS: RGBA change -> new blit!");
	if (rgba_get_refcount(device->cache, *dest_hdl) > 0)
	{
		tmp_hdl = rgba_get_free_surface(device, width, height, format, &tmp_rgba);
		rgba_prepare(tmp_rgba, *dest);

		VDPAU_LOG(LDBG, "RBS: render it on a new surface, copy the old");
		rgba_do_render(tmp_rgba, &d_rect, src, &s_rect);
		rgba_unref(device->cache, *dest_hdl);
		rgba_ref(device->cache, tmp_hdl);
		(*dest)->flags |= RGBA_FLAG_DIRTY;
		*dest_hdl = rgba_set_recently_rendered(device->cache, tmp_hdl, dest);
	}

	pthread_mutex_unlock(&mutex);

	return VDP_STATUS_OK;
}
