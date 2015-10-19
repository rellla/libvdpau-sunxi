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

#include <string.h>
#include <sys/ioctl.h>
#include <inttypes.h>
#include "vdpau_private.h"
#include "ve.h"
#include "g2d_driver.h"
#include "rgba.h"

#define GET_BPP_FROM_FORMAT(rgba_format) (((rgba_format) == VDP_RGBA_FORMAT_A8) ? 1 : 4)
#define VDPFORMAT_TO_G2DFORMAT(rgba_format) (((rgba_format) == VDP_RGBA_FORMAT_A8) ? G2D_FMT_8BPP_MONO :  G2D_FMT_ARGB_AYUV8888)

static void dirty_add_rect(VdpRect *dirty, const VdpRect *rect)
{
	dirty->x0 = min(dirty->x0, rect->x0);
	dirty->y0 = min(dirty->y0, rect->y0);
	dirty->x1 = max(dirty->x1, rect->x1);
	dirty->y1 = max(dirty->y1, rect->y1);
}

static int dirty_in_rect(const VdpRect *dirty, const VdpRect *rect)
{
	return (dirty->x0 >= rect->x0) && (dirty->y0 >= rect->y0) &&
	       (dirty->x1 <= rect->x1) && (dirty->y1 <= rect->y1);
}

static int rect_is_equal(VdpRect const *rect1, VdpRect rect2)
{
	if (rect1->x0 == rect2.x0 &&
	    rect1->y0 == rect2.y0 &&
	    rect1->x1 == rect2.x1 &&
	    rect1->y1 == rect2.y1)
		return 1;

	return 0;
}

static int color_is_equal(VdpColor const *colors1, VdpColor colors2)
{
	if (!colors1)
		return 1;

	if (colors1->red   == colors2.red &&
	    colors1->green == colors2.green &&
	    colors1->blue  == colors2.blue &&
	    colors1->alpha == colors2.alpha)
		return 1;

	return 0;
}

static int blend_state_is_equal(VdpOutputSurfaceRenderBlendState const *blend_state,
				VdpOutputSurfaceRenderBlendState blend_state2)
{
	return 1;
}

static int rgba_changed(rgba_surface_t *dest,
                        VdpRect const *d_rect,
                        rgba_surface_t *src,
                        VdpRect const *s_rect,
                        VdpColor const *colors,
                        VdpOutputSurfaceRenderBlendState const *blend_state,
                        uint32_t flags)
{
	uint32_t id = -1;

	if (src)
		id = src->id;

	/* Check, if any render parameter changed */
	if (rect_is_equal(d_rect, dest->refrgba.d_rect) &&
	    rect_is_equal(s_rect, dest->refrgba.s_rect) &&
	    blend_state_is_equal(blend_state, dest->refrgba.blend_state) &&
	    color_is_equal(colors, dest->refrgba.colors) &&
	    (flags == dest->refrgba.flags) &&
	    (id == dest->refrgba.id))
	{
		/* We have the same rgba already in dest->rgba */
		return 0;
	}

	/* Save the last render parameters as a struct in the output surface */
	dest->refrgba.d_rect.x0 = d_rect->x0;
	dest->refrgba.d_rect.y0 = d_rect->y0;
	dest->refrgba.d_rect.x1 = d_rect->x1;
	dest->refrgba.d_rect.y1 = d_rect->y1;
	dest->refrgba.s_rect.x0 = s_rect->x0;
	dest->refrgba.s_rect.y0 = s_rect->y0;
	dest->refrgba.s_rect.x1 = s_rect->x1;
	dest->refrgba.s_rect.y1 = s_rect->y1;

	if (colors)
	{
		dest->refrgba.colors.red = colors->red;
		dest->refrgba.colors.blue = colors->blue;
		dest->refrgba.colors.green = colors->green;
		dest->refrgba.colors.alpha = colors->alpha;
	}

	dest->refrgba.flags = flags;
	dest->refrgba.id = id;

	return 1;
}

VdpStatus rgba_create(rgba_surface_t *rgba,
                      device_ctx_t *device,
                      uint32_t width,
                      uint32_t height,
                      VdpRGBAFormat format,
                      int idx,
                      int type)
{
	if (format != VDP_RGBA_FORMAT_B8G8R8A8 &&
	    format != VDP_RGBA_FORMAT_R8G8B8A8 &&
	    format != VDP_RGBA_FORMAT_A8)
		return VDP_STATUS_INVALID_RGBA_FORMAT;

	if (width < 1 || width > 8192 || height < 1 || height > 8192)
		return VDP_STATUS_INVALID_SIZE;

	rgba->device = sref(device);
	rgba->width = width;
	rgba->height = height;
	rgba->format = format;

	if (device->flags & DEVICE_FLAG_OSD)
	{
		rgba->data = ve_malloc(width * height * GET_BPP_FROM_FORMAT(rgba->format), idx, type);
		if (!rgba->data)
			return VDP_STATUS_RESOURCES;

		rgba->dirty.x0 = width;
		rgba->dirty.y0 = height;
		rgba->dirty.x1 = 0;
		rgba->dirty.y1 = 0;
		rgba_fill(rgba, NULL, 0x00000000);
		rgba->id = 0;
		rgba->flags = 0;
	}

	return VDP_STATUS_OK;
}

void rgba_destroy(rgba_surface_t *rgba)
{
	if (!rgba->device)
		return;

	if (rgba->device->flags & DEVICE_FLAG_OSD)
		ve_free(rgba->data);

	sfree(rgba->device);
}

VdpStatus rgba_get_bits_native(rgba_surface_t *rgba,
                               VdpRect const *source_rect,
                               void *const *destination_data,
                               uint32_t const *destination_pitches)
{
	if (!(rgba->device->flags & DEVICE_FLAG_OSD))
		return VDP_STATUS_OK;

	VdpRect s_rect = {0, 0, rgba->width, rgba->height};
	if (source_rect)
		s_rect = *source_rect;

	if (0 == s_rect.x0 && rgba->width == s_rect.x1 && destination_pitches[0] == s_rect.x1 * GET_BPP_FROM_FORMAT(rgba->format)) {
		/* full width */
		const int bytes_to_copy =
			(s_rect.x1 - s_rect.x0) * (s_rect.y1 - s_rect.y0) * GET_BPP_FROM_FORMAT(rgba->format);
		memcpy(destination_data[0],
			   rgba->data + s_rect.y0 * rgba->width * GET_BPP_FROM_FORMAT(rgba->format), bytes_to_copy);
	} else {
		const unsigned int bytes_in_line = (s_rect.x1 - s_rect.x0) * GET_BPP_FROM_FORMAT(rgba->format);
		unsigned int y;
		for (y = s_rect.y0; y < s_rect.y1; y ++) {
			memcpy(destination_data[0] + (y - s_rect.y0) * destination_pitches[0],
				   rgba->data + (y * rgba->width + s_rect.x0) * GET_BPP_FROM_FORMAT(rgba->format),
				   bytes_in_line);
		}
	}

	return VDP_STATUS_OK;
}

VdpStatus rgba_put_bits_native(rgba_surface_t *rgba,
                               void const *const *source_data,
                               uint32_t const *source_pitches,
                               VdpRect const *destination_rect)
{
#ifdef DEBUG_TIME
	VdpTime timein, timeout;
#endif
	if (!(rgba->device->flags & DEVICE_FLAG_OSD))
		return VDP_STATUS_OK;

#ifdef DEBUG_TIME
	timein = get_vdp_time();
#endif

	VdpRect d_rect = {0, 0, rgba->width, rgba->height};
	if (destination_rect)
		d_rect = *destination_rect;

	if ((rgba->flags & RGBA_FLAG_NEEDS_CLEAR) && !dirty_in_rect(&rgba->dirty, &d_rect))
		rgba_clear(rgba);

	if ((0 == d_rect.x0) && (rgba->width == d_rect.x1) && (source_pitches[0] == d_rect.x1 * GET_BPP_FROM_FORMAT(rgba->format)))
	{
		/* full line width */
		const int bytes_to_copy = rgba->width * (d_rect.y1 - d_rect.y0) * GET_BPP_FROM_FORMAT(rgba->format);
		memcpy(rgba->data + d_rect.y0 * rgba->width * GET_BPP_FROM_FORMAT(rgba->format),
			   source_data[0],
			   bytes_to_copy);
	}
	else
	{
		const unsigned int bytes_in_line = (d_rect.x1 - d_rect.x0) * GET_BPP_FROM_FORMAT(rgba->format);
		unsigned int y;
		for (y = d_rect.y0; y < d_rect.y1; y ++)
		{
			memcpy(rgba->data + (y * rgba->width + d_rect.x0) * GET_BPP_FROM_FORMAT(rgba->format),
				   source_data[0] + (y - d_rect.y0) * source_pitches[0],
				   bytes_in_line);
		}
	}

	rgba->flags &= ~RGBA_FLAG_NEEDS_CLEAR;
	rgba->flags |= RGBA_FLAG_DIRTY | RGBA_FLAG_NEEDS_FLUSH;
	dirty_add_rect(&rgba->dirty, &d_rect);

	rgba->id++;

#ifdef DEBUG_TIME
	timeout = get_vdp_time();
#endif
	VDPAU_TIME(LSURF1, "PutBitsNative time difference in>out: %" PRIu64 "", ((timeout - timein) / 1000));

	return VDP_STATUS_OK;
}

VdpStatus rgba_put_bits_indexed(rgba_surface_t *rgba,
                                VdpIndexedFormat source_indexed_format,
                                void const *const *source_data,
                                uint32_t const *source_pitch,
                                VdpRect const *destination_rect,
                                VdpColorTableFormat color_table_format,
                                void const *color_table)
{
#ifdef DEBUG_TIME
	VdpTime timein, timeout;
#endif
	if (color_table_format != VDP_COLOR_TABLE_FORMAT_B8G8R8X8)
		return VDP_STATUS_INVALID_COLOR_TABLE_FORMAT;

	if (!(rgba->device->flags & DEVICE_FLAG_OSD))
		return VDP_STATUS_OK;

#ifdef DEBUG_TIME
	timein = get_vdp_time();
#endif
	int x, y;
	const uint32_t *colormap = color_table;
	const uint8_t *src_ptr = source_data[0];
	uint32_t *dst_ptr = rgba->data;

	VdpRect d_rect = {0, 0, rgba->width, rgba->height};
	if (destination_rect)
		d_rect = *destination_rect;

	if ((rgba->flags & RGBA_FLAG_NEEDS_CLEAR) && !dirty_in_rect(&rgba->dirty, &d_rect))
		rgba_clear(rgba);

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

	rgba->flags &= ~RGBA_FLAG_NEEDS_CLEAR;
	rgba->flags |= RGBA_FLAG_DIRTY | RGBA_FLAG_NEEDS_FLUSH;
	dirty_add_rect(&rgba->dirty, &d_rect);

	rgba->id++;

#ifdef DEBUG_TIME
	timeout = get_vdp_time();
#endif
	VDPAU_TIME(LSURF1, "PutBitsIndexed time difference in>out: %" PRIu64 "", ((timeout - timein) / 1000));

	return VDP_STATUS_OK;
}

VdpStatus rgba_render_surface(rgba_surface_t *dest,
                              VdpRect const *destination_rect,
                              rgba_surface_t *src,
                              VdpRect const *source_rect,
                              VdpColor const *colors,
                              VdpOutputSurfaceRenderBlendState const *blend_state,
                              uint32_t flags)
{
	int zerosized = 0;
#ifdef DEBUG_TIME
	VdpTime timein, timeout;
#endif
	if (!(dest->device->flags & DEVICE_FLAG_OSD))
		return VDP_STATUS_OK;

#ifdef DEBUG_TIME
	timein = get_vdp_time();
#endif
	if (colors || flags)
		VDPAU_LOG(LINFO, "%s: colors and flags not implemented!", __func__);

	/* set up source/destination rects using defaults where required */
	VdpRect s_rect = {0, 0, 0, 0};
	VdpRect d_rect = {0, 0, dest->width, dest->height};
	s_rect.x1 = src ? src->width : 0;
	s_rect.y1 = src ? src->height : 0;

	if (source_rect)
		s_rect = *source_rect;
	if (destination_rect)
		d_rect = *destination_rect;

	/* ignore zero-sized surfaces (also workaround for g2d driver bug) */
	if (s_rect.x0 == s_rect.x1 || s_rect.y0 == s_rect.y1 ||
	    d_rect.x0 == d_rect.x1 || d_rect.y0 == d_rect.y1)
		zerosized = 1;

	if (rgba_changed(dest, destination_rect, src, source_rect, colors, blend_state, flags))
	{
		if ((dest->flags & RGBA_FLAG_NEEDS_CLEAR) && !dirty_in_rect(&dest->dirty, &d_rect))
			rgba_clear(dest);
		if (!zerosized)
		{
			if (!src)
				rgba_fill(dest, &d_rect, 0xffffffff);
			else
				rgba_blit(dest, &d_rect, src, &s_rect);

			dirty_add_rect(&dest->dirty, &d_rect);
		}
		VDPAU_LOG(LDBG, "rgba surface changed!");
	}
	else
	{
		VDPAU_LOG(LALL, "rgba surface unchanged!");
	}

	dest->flags &= ~RGBA_FLAG_NEEDS_CLEAR;
	dest->flags |= RGBA_FLAG_DIRTY;

#ifdef DEBUG_TIME
	timeout = get_vdp_time();
	VDPAU_TIME(LSURF1, "RenderSurface time difference in>out: %" PRIu64 "", ((timeout - timein) / 1000));
#endif
	return VDP_STATUS_OK;
}

void rgba_clear(rgba_surface_t *rgba)
{
	if (!(rgba->flags & RGBA_FLAG_DIRTY))
		return;

	rgba_fill(rgba, &rgba->dirty, 0x00000000);
	rgba->flags &= ~(RGBA_FLAG_DIRTY | RGBA_FLAG_NEEDS_CLEAR);
	rgba->dirty.x0 = rgba->width;
	rgba->dirty.y0 = rgba->height;
	rgba->dirty.x1 = 0;
	rgba->dirty.y1 = 0;
}

void rgba_fill(rgba_surface_t *dest, const VdpRect *dest_rect, uint32_t color)
{
	g2d_fillrect args;
#ifdef DEBUG_TIME
	VdpTime timein, timeout;

	timein = get_vdp_time();
#endif
	if (dest->device->flags & DEVICE_FLAG_OSD)
	{
		rgba_flush(dest);
		args.flag = 0;
		args.dst_image.addr[0] = ve_virt2phys(dest->data) + DRAM_OFFSET;
		args.dst_image.w = dest->width;
		args.dst_image.h = dest->height;
		args.dst_image.format = VDPFORMAT_TO_G2DFORMAT(dest->format);
		args.dst_image.pixel_seq = G2D_SEQ_NORMAL;
		if (dest_rect)
		{
			args.dst_rect.x = dest_rect->x0;
			args.dst_rect.y = dest_rect->y0;
			args.dst_rect.w = dest_rect->x1 - dest_rect->x0;
			args.dst_rect.h = dest_rect->y1 - dest_rect->y0;
		}
		else
		{
			args.dst_rect.x = 0;
			args.dst_rect.y = 0;
			args.dst_rect.w = dest->width;
			args.dst_rect.h = dest->height;
		}
		args.flag |= G2D_FIL_PIXEL_ALPHA;
		args.color = color & 0xffffff;

		ioctl(dest->device->g2d_fd, G2D_CMD_FILLRECT, &args);
	}
#ifdef DEBUG_TIME
	timeout = get_vdp_time();
#endif
	VDPAU_TIME(LSURF2, "Filling time difference in>out: %" PRIu64 "", ((timeout - timein) / 1000));
}

void rgba_blit(rgba_surface_t *dest, const VdpRect *dest_rect, rgba_surface_t *src, const VdpRect *src_rect)
{
	g2d_blt args;
#ifdef DEBUG_TIME
	VdpTime timein, timeout;

	timein = get_vdp_time();
#endif
	if (dest->device->flags & DEVICE_FLAG_OSD)
	{
		rgba_flush(dest);
		rgba_flush(src);

		args.flag = 0;
		args.src_image.addr[0] = ve_virt2phys(src->data) + DRAM_OFFSET;
		args.src_image.w = src->width;
		args.src_image.h = src->height;
		args.src_image.format = VDPFORMAT_TO_G2DFORMAT(src->format);
		args.src_image.pixel_seq = G2D_SEQ_NORMAL;
		args.src_rect.x = src_rect->x0;
		args.src_rect.y = src_rect->y0;
		args.src_rect.w = src_rect->x1 - src_rect->x0;
		args.src_rect.h = src_rect->y1 - src_rect->y0;
		args.dst_image.addr[0] = ve_virt2phys(dest->data) + DRAM_OFFSET;
		args.dst_image.w = dest->width;
		args.dst_image.h = dest->height;
		args.dst_image.format = VDPFORMAT_TO_G2DFORMAT(dest->format);
		args.dst_image.pixel_seq = G2D_SEQ_NORMAL;
		args.dst_x = dest_rect->x0;
		args.dst_y = dest_rect->y0;
		if (dest->flags & RGBA_FLAG_NEEDS_CLEAR)
			args.flag |= G2D_BLT_NONE;
		else
			args.flag |= G2D_BLT_PIXEL_ALPHA;
		args.color = 0;

		ioctl(dest->device->g2d_fd, G2D_CMD_BITBLT, &args);
	}
#ifdef DEBUG_TIME
	timeout = get_vdp_time();
#endif
	VDPAU_TIME(LSURF2, "Blitting time difference in>out: %" PRIu64 "", ((timeout - timein) / 1000));
}

void rgba_flush(rgba_surface_t *rgba)
{
	if (rgba->flags & RGBA_FLAG_NEEDS_FLUSH)
	{
		ve_flush_cache(rgba->data + rgba->dirty.y0 * rgba->width * GET_BPP_FROM_FORMAT(rgba->format),
				(rgba->dirty.y1 - rgba->dirty.y0) * rgba->width * GET_BPP_FROM_FORMAT(rgba->format));
		rgba->flags &= ~RGBA_FLAG_NEEDS_FLUSH;
	}
}
