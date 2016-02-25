/*
 * Copyright (c) 2016 Andreas Baierl <ichgeh@imkreisrum.de>
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

#ifndef __NV_INTEROP_H__
#define __NV_INTEROP_H__

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <EGL/eglplatform.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <ump/ump.h>
#include <ump/ump_ref_drv.h>
#include <cedrus/cedrus.h>

#define MAX_NUM_TEXTURES 4
#define USE_TILE 0

/* GLES interop */
#define SURFACE_STATE_NV			0x86EB
#define SURFACE_REGISTERED_NV			0x86FD
#define SURFACE_MAPPED_NV			0x8700
#define READ_ONLY				0x88B8
#define READ_WRITE				0x88BA
#define WRITE_DISCARD_NV			0x88BE

#define VDP_FUNC_ID_Init_NV			(VdpFuncId)100
#define VDP_FUNC_ID_Fini_NV			(VdpFuncId)101
#define VDP_FUNC_ID_RegisterVideoSurface_NV	(VdpFuncId)102
#define VDP_FUNC_ID_RegisterOutputSurface_NV	(VdpFuncId)103
#define VDP_FUNC_ID_IsSurface_NV		(VdpFuncId)104
#define VDP_FUNC_ID_UnregisterSurface_NV	(VdpFuncId)105
#define VDP_FUNC_ID_GetSurfaceiv_NV		(VdpFuncId)106
#define VDP_FUNC_ID_SurfaceAccess_NV		(VdpFuncId)107
#define VDP_FUNC_ID_MapSurfaces_NV		(VdpFuncId)108
#define VDP_FUNC_ID_UnmapSurfaces_NV		(VdpFuncId)109

#ifdef __cplusplus
extern "C" {
#endif

/* FBDEV */
typedef enum
{
	FBDEV_PIXMAP_SUPPORTS_UMP = (1 << 0)
} fbdev_pixmap_flags;

typedef struct fbdev_pixmap
{
	unsigned int height;
	unsigned int width;
	unsigned int bytes_per_pixel;
	unsigned char buffer_size;
	unsigned char red_size;
	unsigned char green_size;
	unsigned char blue_size;
	unsigned char alpha_size;
	unsigned char luminance_size;
	fbdev_pixmap_flags flags;
	unsigned short *data;
	unsigned int format;
} fbdev_pixmap;


typedef GLintptr vdpauSurfaceNV;

enum VdpauNVState
{
	NV_UNREGISTERED = 0,
	NV_REGISTERED,
	NV_MAPPED
};

enum VdpauNVAccess
{
	NV_READ_ONLY = 1,
	NV_READ_WRITE,
	NV_WRITE_DISCARD_NV
};

enum VdpauNVSurfaceType
{
	NV_SURFACE_RGBA = 0,
	NV_SURFACE_VIDEO
};

enum color_plane
{
	y_plane,
	uv_plane,
	argb_plane
};

typedef struct
{
	enum VdpauNVState state;
	enum VdpauNVSurfaceType type;
	enum VdpauNVAccess access;
	void *vdpsurface;
	uint32_t target;
	GLsizei numTextureNames;
	uint textureNames[MAX_NUM_TEXTURES];
	struct fbdev_pixmap pixmap[MAX_NUM_TEXTURES];
	EGLImageKHR eglImage[MAX_NUM_TEXTURES];
	uint32_t conv_width, conv_height;
	cedrus_mem_t *yuvY;
	cedrus_mem_t *yuvUV;
} nv_surface_ctx_t;

void glVDPAUInitNV(const void *vdpDevice,
		   const void *getProcAddress,
		   EGLContext shared_context,
		   EGLDisplay shared_display);
void glVDPAUFiniNV(void);
vdpauSurfaceNV glVDPAURegisterVideoSurfaceNV(const void *vdpSurface,
					     uint32_t target,
					     GLsizei numTextureNames,
					     const uint *textureNames);
vdpauSurfaceNV glVDPAURegisterOutputSurfaceNV(const void *vdpSurface,
					      uint32_t target,
					      GLsizei numTextureNames,
					      const uint *textureNames);
int glVDPAUIsSurfaceNV(vdpauSurfaceNV surface);
void glVDPAUUnregisterSurfaceNV(vdpauSurfaceNV surface);
void glVDPAUGetSurfaceivNV(vdpauSurfaceNV surface,
			   uint32_t pname,
			   GLsizei bufSize,
			   GLsizei *length,
			   int *values);
void glVDPAUSurfaceAccessNV(vdpauSurfaceNV surface,
			    uint32_t access);
void glVDPAUMapSurfacesNV(GLsizei numSurfaces,
			  const vdpauSurfaceNV *surfaces);
void glVDPAUUnmapSurfacesNV(GLsizei numSurfaces,
			  const vdpauSurfaceNV *surfaces);

#ifdef __cplusplus
}
#endif

#endif
