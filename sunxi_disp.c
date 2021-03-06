/*
 * Copyright (c) 2013-2015 Jens Kuske <jenskuske@gmail.com>
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

#include <fcntl.h>
#include <math.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include "kernel-headers/sunxi_disp_ioctl.h"
#include "vdpau_private.h"
#include "sunxi_disp.h"

struct sunxi_disp_private
{
	struct sunxi_disp pub;

	int fd;
	int fb;
	int video_layer;
	int osd_layer;
	int video_active;
	__disp_layer_info_t video_info;
	__disp_layer_info_t osd_info;
	__disp_video_fb_t videofb_info;
};

static void sunxi_disp_close(struct sunxi_disp *sunxi_disp);
static int sunxi_disp_set_video_layer(struct sunxi_disp *sunxi_disp, int x, int y, int width, int height, output_surface_ctx_t *surface);
static void sunxi_disp_close_video_layer(struct sunxi_disp *sunxi_disp);
static int sunxi_disp_set_osd_layer(struct sunxi_disp *sunxi_disp, int x, int y, int width, int height, output_surface_ctx_t *surface);
static void sunxi_disp_close_osd_layer(struct sunxi_disp *sunxi_disp);
static int sunxi_disp_wait_for_vsync(struct sunxi_disp *sunxi_disp);

struct sunxi_disp *sunxi_disp_open(int osd_enabled)
{
	struct sunxi_disp_private *disp = calloc(1, sizeof(*disp));

	disp->fd = open("/dev/disp", O_RDWR);
	if (disp->fd == -1)
		goto err_open;

	int tmp = SUNXI_DISP_VERSION;
	if (ioctl(disp->fd, DISP_CMD_VERSION, &tmp) < 0)
		goto err_version;

	uint32_t args[4] = { 0, DISP_LAYER_WORK_MODE_SCALER, 0, 0 };
	disp->video_layer = ioctl(disp->fd, DISP_CMD_LAYER_REQUEST, args);
	if (disp->video_layer == 0)
		goto err_video_layer;

	args[1] = disp->video_layer;
	ioctl(disp->fd, osd_enabled ? DISP_CMD_LAYER_TOP : DISP_CMD_LAYER_BOTTOM, args);

	disp->fb = open("/dev/fb0", O_RDWR);
	if (disp->fb == -1)
		VDPAU_DBG("VSync disabled.");

	if (osd_enabled)
	{
		args[1] = DISP_LAYER_WORK_MODE_NORMAL;
		disp->osd_layer = ioctl(disp->fd, DISP_CMD_LAYER_REQUEST, args);
		if (disp->osd_layer == 0)
			goto err_osd_layer;

		args[1] = disp->osd_layer;
		ioctl(disp->fd, DISP_CMD_LAYER_TOP, args);

		disp->osd_info.pipe = 1;
		disp->osd_info.mode = DISP_LAYER_WORK_MODE_NORMAL;
		disp->osd_info.fb.mode = DISP_MOD_INTERLEAVED;
		disp->osd_info.fb.format = DISP_FORMAT_ARGB8888;
		disp->osd_info.fb.seq = DISP_SEQ_ARGB;
		disp->osd_info.fb.cs_mode = DISP_BT601;
	}
	else
	{
		disp->video_info.pipe = 1;
		disp->video_info.ck_enable = 1;

		__disp_colorkey_t ck;
		ck.ck_max.red = ck.ck_min.red = 0;
		ck.ck_max.green = ck.ck_min.green = 1;
		ck.ck_max.blue = ck.ck_min.blue = 2;
		ck.red_match_rule = 2;
		ck.green_match_rule = 2;
		ck.blue_match_rule = 2;

		args[1] = (unsigned long)(&ck);
		ioctl(disp->fd, DISP_CMD_SET_COLORKEY, args);
	}

	disp->video_info.mode = DISP_LAYER_WORK_MODE_SCALER;
	disp->video_info.fb.cs_mode = DISP_BT601;
	disp->video_info.fb.br_swap = 0;

	disp->pub.close = sunxi_disp_close;
	disp->pub.set_video_layer = sunxi_disp_set_video_layer;
	disp->pub.close_video_layer = sunxi_disp_close_video_layer;
	disp->pub.set_osd_layer = sunxi_disp_set_osd_layer;
	disp->pub.close_osd_layer = sunxi_disp_close_osd_layer;
	disp->pub.wait_for_vsync = sunxi_disp_wait_for_vsync;
	disp->pub.deint_enabled = 1;

	return (struct sunxi_disp *)disp;

err_osd_layer:
	args[1] = disp->osd_layer;
	ioctl(disp->fd, DISP_CMD_LAYER_RELEASE, args);
err_video_layer:
err_version:
	close(disp->fd);
err_open:
	free(disp);
	return NULL;
}

static void sunxi_disp_close(struct sunxi_disp *sunxi_disp)
{
	struct sunxi_disp_private *disp = (struct sunxi_disp_private *)sunxi_disp;
	uint32_t args[4] = { 0, 0, 0, 0 };

	if (disp->video_layer)
	{
		args[1] = disp->video_layer;
		ioctl(disp->fd, DISP_CMD_VIDEO_STOP, args);
		disp->video_active = 0;
		ioctl(disp->fd, DISP_CMD_LAYER_CLOSE, args);
		ioctl(disp->fd, DISP_CMD_LAYER_RELEASE, args);
	}

	if (disp->osd_layer)
	{
		args[1] = disp->osd_layer;
		ioctl(disp->fd, DISP_CMD_LAYER_CLOSE, args);
		ioctl(disp->fd, DISP_CMD_LAYER_RELEASE, args);
	}

	if(disp->fb != -1)
		close(disp->fb);

	close(disp->fd);
	free(sunxi_disp);
}

static int sunxi_disp_set_video_layer(struct sunxi_disp *sunxi_disp, int x, int y, int width, int height, output_surface_ctx_t *surface)
{
	static int last_id;
	struct sunxi_disp_private *disp = (struct sunxi_disp_private *)sunxi_disp;

	uint32_t args[4] = { 0, disp->video_layer, 0, 0 };

	if (surface->reinit_disp)
	{
		last_id = -1;

		switch (surface->vs->source_format) {
		case VDP_YCBCR_FORMAT_YUYV:
			disp->video_info.fb.mode = DISP_MOD_INTERLEAVED;
			disp->video_info.fb.format = DISP_FORMAT_YUV422;
			disp->video_info.fb.seq = DISP_SEQ_YUYV;
			break;
		case VDP_YCBCR_FORMAT_UYVY:
			disp->video_info.fb.mode = DISP_MOD_INTERLEAVED;
			disp->video_info.fb.format = DISP_FORMAT_YUV422;
			disp->video_info.fb.seq = DISP_SEQ_UYVY;
			break;
		case VDP_YCBCR_FORMAT_NV12:
			disp->video_info.fb.mode = DISP_MOD_NON_MB_UV_COMBINED;
			disp->video_info.fb.format = DISP_FORMAT_YUV420;
			disp->video_info.fb.seq = DISP_SEQ_UVUV;
			break;
		case VDP_YCBCR_FORMAT_YV12:
			disp->video_info.fb.mode = DISP_MOD_NON_MB_PLANAR;
			disp->video_info.fb.format = DISP_FORMAT_YUV420;
			disp->video_info.fb.seq = DISP_SEQ_UVUV;
			break;
		default:
		case INTERNAL_YCBCR_FORMAT:
			disp->video_info.fb.mode = DISP_MOD_MB_UV_COMBINED;
			disp->video_info.fb.format = DISP_FORMAT_YUV420;
			disp->video_info.fb.seq = DISP_SEQ_UVUV;
			break;
		}

		disp->video_info.fb.addr[0] = cedrus_mem_get_phys_addr(surface->yuv->data);
		disp->video_info.fb.addr[1] = cedrus_mem_get_phys_addr(surface->yuv->data) + surface->vs->luma_size;
		disp->video_info.fb.addr[2] = cedrus_mem_get_phys_addr(surface->yuv->data) + surface->vs->luma_size + surface->vs->chroma_size / 2;

		disp->video_info.fb.size.width = surface->vs->width;
		disp->video_info.fb.size.height = surface->vs->height;
		disp->video_info.src_win.x = surface->video_src_rect.x0;
		disp->video_info.src_win.y = surface->video_src_rect.y0;
		disp->video_info.src_win.width = surface->video_src_rect.x1 - surface->video_src_rect.x0;
		disp->video_info.src_win.height = surface->video_src_rect.y1 - surface->video_src_rect.y0;
		disp->video_info.scn_win.x = x + surface->video_dst_rect.x0;
		disp->video_info.scn_win.y = y + surface->video_dst_rect.y0;
		disp->video_info.scn_win.width = surface->video_dst_rect.x1 - surface->video_dst_rect.x0;
		disp->video_info.scn_win.height = surface->video_dst_rect.y1 - surface->video_dst_rect.y0;

		if (disp->video_info.scn_win.y < 0)
		{
			int scn_clip = -(disp->video_info.scn_win.y);
			int src_clip = scn_clip * disp->video_info.src_win.height / disp->video_info.scn_win.height;
			disp->video_info.src_win.y += src_clip;
			disp->video_info.src_win.height -= src_clip;
			disp->video_info.scn_win.y = 0;
			disp->video_info.scn_win.height -= scn_clip;
		}

		args[2] = (unsigned long)(&disp->video_info);
		ioctl(disp->fd, DISP_CMD_LAYER_SET_PARA, args);

		ioctl(disp->fd, DISP_CMD_LAYER_OPEN, args);
		ioctl(disp->fd, DISP_CMD_VIDEO_START, args);
		disp->video_active = 1;
	}
	else
	{
		if (disp->video_active == 0)
		{
			uint32_t args[4] = { 0, disp->video_layer, 0, 0 };
			ioctl(disp->fd, DISP_CMD_VIDEO_START, args);
		}
		disp->videofb_info.id = last_id + 1;
		disp->videofb_info.addr[0] = cedrus_mem_get_phys_addr(surface->yuv->data);
		disp->videofb_info.addr[1] = cedrus_mem_get_phys_addr(surface->yuv->data) + surface->vs->luma_size;
		disp->videofb_info.addr[2] = cedrus_mem_get_phys_addr(surface->yuv->data) + surface->vs->luma_size + surface->vs->chroma_size / 2;
		disp->videofb_info.interlace = surface->vs->video_deinterlace;
		disp->videofb_info.top_field_first = surface->vs->video_field ? 0 : 1;

		args[2] = (unsigned long)(&disp->videofb_info);
		if (ioctl(disp->fd, DISP_CMD_VIDEO_SET_FB, args))
			VDPAU_DBG("DISP_CMD_VIDEO_SET_FB failed");
		last_id++;

		ioctl(disp->fd, DISP_CMD_LAYER_OPEN, args);
	}

	if (surface->csc_change)
	{
		uint32_t b, c, s, h;

		ioctl(disp->fd, DISP_CMD_LAYER_ENHANCE_OFF, args);

		/* scale VDPAU: -1.0 ~ 1.0 to SUNXI: 0 ~ 100 */
		b = args[2] = ((surface->brightness + 1.0) * 50.0) + 0.5;
		ioctl(disp->fd, DISP_CMD_LAYER_SET_BRIGHT, args);

		/* scale VDPAU: 0.0 ~ 10.0 to SUNXI: 0 ~ 100 */
		if (surface->contrast <= 1.0)
			c = args[2] = (surface->contrast * 50.0) + 0.5;
		else
			c = args[2] = (50.0 + (surface->contrast - 1.0) * 50.0 / 9.0) + 0.5;
		ioctl(disp->fd, DISP_CMD_LAYER_SET_CONTRAST, args);

		/* scale VDPAU: 0.0 ~ 10.0 to SUNXI: 0 ~ 100 */
		if (surface->saturation <= 1.0)
			s = args[2] = (surface->saturation * 50.0) + 0.5;
		else
			s = args[2] = (50.0 + (surface->saturation - 1.0) * 50.0 / 9.0) + 0.5;
		ioctl(disp->fd, DISP_CMD_LAYER_SET_SATURATION, args);

		/* scale VDPAU: -PI ~ PI   to SUNXI: 0 ~ 100 */
		h = args[2] = (((surface->hue / M_PI) + 1.0) * 50.0) + 0.5;
		ioctl(disp->fd, DISP_CMD_LAYER_SET_HUE, args);

		ioctl(disp->fd, DISP_CMD_LAYER_ENHANCE_ON, args);

		VDPAU_DBG("Presentation queue csc change");
		VDPAU_DBG("display driver -> bright: %d, contrast: %d, saturation: %d, hue: %d", b, c, s, h);
		surface->csc_change = 0;
	}

	return 0;
}

static void sunxi_disp_close_video_layer(struct sunxi_disp *sunxi_disp)
{
	struct sunxi_disp_private *disp = (struct sunxi_disp_private *)sunxi_disp;

	uint32_t args[4] = { 0, disp->video_layer, 0, 0 };
	ioctl(disp->fd, DISP_CMD_VIDEO_STOP, args);
	disp->video_active = 0;
	ioctl(disp->fd, DISP_CMD_LAYER_CLOSE, args);
}

static int sunxi_disp_set_osd_layer(struct sunxi_disp *sunxi_disp, int x, int y, int width, int height, output_surface_ctx_t *surface)
{
	struct sunxi_disp_private *disp = (struct sunxi_disp_private *)sunxi_disp;

	switch (surface->rgba.format)
	{
	case VDP_RGBA_FORMAT_R8G8B8A8:
		disp->osd_info.fb.br_swap = 1;
		break;
	case VDP_RGBA_FORMAT_B8G8R8A8:
	default:
		disp->osd_info.fb.br_swap = 0;
		break;
	}

	disp->osd_info.fb.addr[0] = cedrus_mem_get_phys_addr(surface->rgba.data);
	disp->osd_info.fb.size.width = surface->rgba.width;
	disp->osd_info.fb.size.height = surface->rgba.height;
	disp->osd_info.src_win.x = surface->rgba.dirty.x0;
	disp->osd_info.src_win.y = surface->rgba.dirty.y0;
	disp->osd_info.src_win.width = surface->rgba.dirty.x1 - surface->rgba.dirty.x0;
	disp->osd_info.src_win.height = surface->rgba.dirty.y1 - surface->rgba.dirty.y0;
	disp->osd_info.scn_win.x = x + surface->rgba.dirty.x0;
	disp->osd_info.scn_win.y = y + surface->rgba.dirty.y0;
	disp->osd_info.scn_win.width = min_nz(width, surface->rgba.dirty.x1) - surface->rgba.dirty.x0;
	disp->osd_info.scn_win.height = min_nz(height, surface->rgba.dirty.y1) - surface->rgba.dirty.y0;

	uint32_t args[4] = { 0, disp->osd_layer, (unsigned long)(&disp->osd_info), 0 };
	ioctl(disp->fd, DISP_CMD_LAYER_SET_PARA, args);

	ioctl(disp->fd, DISP_CMD_LAYER_OPEN, args);

	return 0;
}

static void sunxi_disp_close_osd_layer(struct sunxi_disp *sunxi_disp)
{
	struct sunxi_disp_private *disp = (struct sunxi_disp_private *)sunxi_disp;

	uint32_t args[4] = { 0, disp->osd_layer, 0, 0 };
	ioctl(disp->fd, DISP_CMD_LAYER_CLOSE, args);
}

static int sunxi_disp_wait_for_vsync(struct sunxi_disp *sunxi_disp)
{
	struct sunxi_disp_private *disp = (struct sunxi_disp_private *)sunxi_disp;

	if(disp->fb != -1)
		return ioctl(disp->fb, FBIO_WAITFORVSYNC, 0);

	return 0;
}
