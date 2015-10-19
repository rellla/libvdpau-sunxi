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

#include <string.h>
#include <inttypes.h>
#include "vdpau_private.h"
#include "ve.h"

static void cleanup_decoder(void *ptr, void *meta)
{
	decoder_ctx_t *decoder = ptr;

	if (decoder->private_free)
		decoder->private_free(decoder);

	ve_free(decoder->data);

	sfree(decoder->device);
}

VdpStatus vdp_decoder_create(VdpDevice device,
                             VdpDecoderProfile profile,
                             uint32_t width,
                             uint32_t height,
                             uint32_t max_references,
                             VdpDecoder *decoder)
{
	static int idx = 0;

	smart device_ctx_t *dev = handle_get(device);
	if (!dev)
		return VDP_STATUS_INVALID_HANDLE;

	if (max_references > 16)
		return VDP_STATUS_ERROR;

	smart decoder_ctx_t *dec = handle_alloc(sizeof(*dec), cleanup_decoder);
	if (!dec)
		return VDP_STATUS_RESOURCES;

	dec->device = sref(dev);
	dec->profile = profile;
	dec->width = width;
	dec->height = height;
	dec->idx = idx++;

	dec->data = ve_malloc(VBV_SIZE, dec->idx, DECODER);
	if (!(dec->data))
		return VDP_STATUS_RESOURCES;

	VdpStatus ret;
	switch (profile)
	{
	case VDP_DECODER_PROFILE_MPEG1:
	case VDP_DECODER_PROFILE_MPEG2_SIMPLE:
	case VDP_DECODER_PROFILE_MPEG2_MAIN:
		ret = new_decoder_mpeg12(dec);
		break;

	case VDP_DECODER_PROFILE_H264_BASELINE:
	case VDP_DECODER_PROFILE_H264_MAIN:
	case VDP_DECODER_PROFILE_H264_HIGH:
		ret = new_decoder_h264(dec);
		break;

	case VDP_DECODER_PROFILE_MPEG4_PART2_SP:
	case VDP_DECODER_PROFILE_MPEG4_PART2_ASP:
		ret = new_decoder_mpeg4(dec);
		break;

	default:
		ret = VDP_STATUS_INVALID_DECODER_PROFILE;
		break;
	}

	if (ret != VDP_STATUS_OK)
		return VDP_STATUS_ERROR;

	VDPAU_LOG(LINFO, "Decoder created");
	ve_dumpmem();

	return handle_create(decoder, dec);
}

VdpStatus vdp_decoder_get_parameters(VdpDecoder decoder,
                                     VdpDecoderProfile *profile,
                                     uint32_t *width,
                                     uint32_t *height)
{
	smart decoder_ctx_t *dec = handle_get(decoder);
	if (!dec)
		return VDP_STATUS_INVALID_HANDLE;

	if (profile)
		*profile = dec->profile;

	if (width)
		*width = dec->width;

	if (height)
		*height = dec->height;

	return VDP_STATUS_OK;
}

VdpStatus vdp_decoder_render(VdpDecoder decoder,
                             VdpVideoSurface target,
                             VdpPictureInfo const *picture_info,
                             uint32_t bitstream_buffer_count,
                             VdpBitstreamBuffer const *bitstream_buffers)
{
#ifdef DEBUG_TIME
	VdpTime timein, timeout;
#endif
	VdpStatus ret;

	smart decoder_ctx_t *dec = handle_get(decoder);
	if (!dec)
		return VDP_STATUS_INVALID_HANDLE;

	smart video_surface_ctx_t *vid = handle_get(target);
	if (!vid)
		return VDP_STATUS_INVALID_HANDLE;

#ifdef DEBUG_TIME
	timein = get_vdp_time();
#endif

	vid->source_format = INTERNAL_YCBCR_FORMAT;
	unsigned int i, pos = 0;

	for (i = 0; i < bitstream_buffer_count; i++)
	{
		memcpy(dec->data + pos, bitstream_buffers[i].bitstream, bitstream_buffers[i].bitstream_bytes);
		pos += bitstream_buffers[i].bitstream_bytes;
	}
	ve_flush_cache(dec->data, pos);

	ret = dec->decode(dec, picture_info, pos, vid);

#ifdef DEBUG_TIME
	timeout = get_vdp_time();
#endif
	VDPAU_TIME(LDEC, "Decoder time difference in>out: %" PRIu64 "", ((timeout - timein) / 1000));

	return ret;
}

VdpStatus vdp_decoder_query_capabilities(VdpDevice device,
                                         VdpDecoderProfile profile,
                                         VdpBool *is_supported,
                                         uint32_t *max_level,
                                         uint32_t *max_macroblocks,
                                         uint32_t *max_width,
                                         uint32_t *max_height)
{
	if (!is_supported || !max_level || !max_macroblocks || !max_width || !max_height)
		return VDP_STATUS_INVALID_POINTER;

	smart device_ctx_t *dev = handle_get(device);
	if (!dev)
		return VDP_STATUS_INVALID_HANDLE;

	*max_width = 3840;
	*max_height = 2160;
	*max_macroblocks = (*max_width * *max_height) / (16 * 16);

	switch (profile)
	{
	case VDP_DECODER_PROFILE_MPEG1:
		*max_level = VDP_DECODER_LEVEL_MPEG1_NA;
		*is_supported = VDP_TRUE;
		break;
	case VDP_DECODER_PROFILE_MPEG2_SIMPLE:
	case VDP_DECODER_PROFILE_MPEG2_MAIN:
		*max_level = VDP_DECODER_LEVEL_MPEG2_HL;
		*is_supported = VDP_TRUE;
		break;
	case VDP_DECODER_PROFILE_H264_BASELINE:
	case VDP_DECODER_PROFILE_H264_MAIN:
	case VDP_DECODER_PROFILE_H264_HIGH:
	case VDP_DECODER_PROFILE_MPEG4_PART2_SP:
	case VDP_DECODER_PROFILE_MPEG4_PART2_ASP:
		*max_level = VDP_DECODER_LEVEL_H264_5_1;
		*is_supported = VDP_TRUE;
		break;

	default:
		*is_supported = VDP_FALSE;
		break;
	}

	return VDP_STATUS_OK;
}
