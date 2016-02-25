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

#include "vdpau_private.h"
#include "nv_interop.h"
#include "rgba.h"
#include "tiled_yuv.h"

#define GLDEBUG

/* "/srv/public" must be writeable */
//#define GLFILE

static PFNEGLCREATEIMAGEKHRPROC peglCreateImageKHR = NULL;
static PFNEGLDESTROYIMAGEKHRPROC peglDestroyImageKHR = NULL;
static PFNGLEGLIMAGETARGETTEXTURE2DOESPROC pglEGLImageTargetTexture2DOES = NULL;
static EGLDisplay eglDisplay;
static EGLContext eglSharedContext = EGL_NO_CONTEXT;
static EGLContext eglContext = EGL_NO_CONTEXT;
static EGLSurface eglSurface = EGL_NO_SURFACE;
static const void *ctx_vdpDevice = 0;
static const void *ctx_vdpGetProcAddress = 0;

void eglCheckError(const char *stmt, const char* fname, int line)
{
	EGLint err = eglGetError();
	if (err != EGL_SUCCESS)
	{
		VDPAU_DBG("INTEROP: EGL Error 0x%08x, %s failed at %s:%i.", err, stmt, fname, line);
		abort();
	}
}

void glCheckError(const char *stmt, const char* fname, int line)
{
	GLint err = glGetError();
	if (err != GL_NO_ERROR)
	{
		VDPAU_DBG("INTEROP: GL Error 0x%08x, %s falied at %s:%i.", err, stmt, fname, line);
		abort();
	}
}

#ifdef GLDEBUG
#define GL_CHECK(stmt) do { \
	stmt; \
	glCheckError(#stmt, __FILE__, __LINE__); \
	} while (0)

#define EGL_CHECK(stmt) do { \
	stmt; \
	eglCheckError(#stmt, __FILE__, __LINE__); \
	} while (0)
#else
#define GL_CHECK(stmt) stmt
#define EGL_CHECK(stmt) stmt
#endif

void vdp_eglAcquireContext()
{
	EGL_CHECK(eglMakeCurrent(eglDisplay, eglSurface, eglSurface, eglContext));
}

void vdp_eglReleaseContext()
{
	EGL_CHECK(eglMakeCurrent(eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT));
}

static void cleanup_surface_nv(void *ptr, void *meta)
{
}

void glVDPAUInitNV(const void *vdpDevice,
		   const void *getProcAddress,
		   EGLContext shared_context,
		   EGLDisplay shared_display)
{
	if (!vdpDevice)
	{
		VDPAU_DBG("INTEROP: glVDPAUInitNV vdpDevice failed");
		return;
	}

	if (!getProcAddress)
	{
		VDPAU_DBG("INTEROP: glVDPAUInitNV getProcAddress failed");
		return;
	}

	if (ctx_vdpDevice || ctx_vdpGetProcAddress)
	{
		VDPAU_DBG("INTEROP: glVDPAUInitNV failed, ctx already exists");
		return;
	}

	if (shared_context == EGL_NO_CONTEXT)
	{
		VDPAU_DBG("INTEROP: glVDPAUInitNV failed, no shared context");
		return;
	}

	eglDisplay = shared_display;
	eglSharedContext = shared_context;

	EGL_CHECK(peglCreateImageKHR =
		(PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR"));
	if (peglCreateImageKHR == NULL)
		VDPAU_DBG("INTEROP: eglCreateImageKHR not found!");

	EGL_CHECK(peglDestroyImageKHR =
		(PFNEGLDESTROYIMAGEKHRPROC)eglGetProcAddress("eglDestroyImageKHR"));
	if (peglDestroyImageKHR == NULL)
		VDPAU_DBG("INTEROP: eglDestroyImageKHR not found!");

	EGL_CHECK(pglEGLImageTargetTexture2DOES =
		(PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)eglGetProcAddress("glEGLImageTargetTexture2DOES"));
	if (pglEGLImageTargetTexture2DOES == NULL)
		VDPAU_DBG("INTEROP: glEGLImageTargetTexture2DOES not found!");

	EGLConfig eglConfig = 0;
	EGL_CHECK(eglBindAPI(EGL_OPENGL_ES_API));

	int iConfigs;
	EGLint pi32ConfigAttribs[] = {
		EGL_RED_SIZE, 8,
		EGL_BLUE_SIZE, 8,
		EGL_GREEN_SIZE, 8,
		EGL_ALPHA_SIZE, 8,
		EGL_BUFFER_SIZE, 32,
		EGL_STENCIL_SIZE, 0,
		EGL_DEPTH_SIZE, 0,
		EGL_SAMPLES, 4,
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT | EGL_PBUFFER_BIT | EGL_PIXMAP_BIT,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_NONE
	};

	EGLBoolean ret;
	EGL_CHECK(ret = eglChooseConfig(eglDisplay,
			     pi32ConfigAttribs,
			     &eglConfig,
			     1,
			     &iConfigs) || (iConfigs != 1));
	if (!ret)
	{
		VDPAU_DBG("INTEROP: glVDPAUInitNV failed, no eglConfig found");
		return;
	}

	EGL_CHECK(eglSurface = eglCreatePbufferSurface(eglDisplay, eglConfig, NULL));
	EGLint ai32ContextAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	EGL_CHECK(eglContext = eglCreateContext(eglDisplay, eglConfig, eglSharedContext, ai32ContextAttribs));

	ctx_vdpDevice = vdpDevice;
	ctx_vdpGetProcAddress = getProcAddress;

	VDPAU_DBG("INTEROP: glVDPAUInitNV success");
}

void glVDPAUFiniNV(void)
{
	if (!ctx_vdpDevice || !ctx_vdpGetProcAddress)
	{
		VDPAU_DBG("INTEROP: Error ctx");
		return;
	}

	if (eglSharedContext == EGL_NO_CONTEXT)
	{
		VDPAU_DBG("INTEROP: glVDPAUInitNV failed, no shared context");
		return;
	}

	vdp_eglAcquireContext();
	if (eglSurface != EGL_NO_SURFACE)
	{
		EGL_CHECK(eglDestroySurface(eglDisplay, eglSurface));
		eglSurface = EGL_NO_SURFACE;
	}

	if (eglContext != EGL_NO_CONTEXT)
	{
		EGL_CHECK(eglDestroyContext(eglDisplay, eglContext));
		eglContext = EGL_NO_CONTEXT;
	}
	vdp_eglReleaseContext();

	ctx_vdpDevice = 0;
	ctx_vdpGetProcAddress = 0;

	VDPAU_DBG("INTEROP: glVDPAUFiniNV success");
}

static void createNativePixmap(fbdev_pixmap *pm,
			    nv_surface_ctx_t *nv,
			    void *surface,
			    enum color_plane cp,
			    int isOutputSurface)
{
	cedrus_mem_t *mem = NULL;
	int buffer_size = 8;
	int red_size = 0;
	int green_size = 0;
	int blue_size = 0;
	int alpha_size = 0;
	int luminance_size = 8;
	int width = 0;
	int height = 0;
	ump_handle id;

	if (isOutputSurface)
	{
		output_surface_ctx_t *surf = (output_surface_ctx_t *)surface;
		switch (cp)
		{
			case (argb_plane):
				buffer_size = 32;
				luminance_size = 0;
				red_size = 8;
				green_size = 8;
				blue_size = 8;
				alpha_size = 8;
				mem = surf->rgba.data;
				width = surf->rgba.width;
				height = surf->rgba.height;
				break;
			default:
				break;
		}
	}
	else
	{
		switch (cp)
		{
			case (y_plane):
				mem = nv->yuvY;
				width = nv->conv_width;
				height = nv->conv_height;
				break;
			case (uv_plane):
				buffer_size = 16;
				alpha_size = 8;
				mem = nv->yuvUV;
				width = nv->conv_width;
				height = (nv->conv_height + 1) / 2; 
				break;
			default:
				break;
		}
	}

	pm->bytes_per_pixel = buffer_size / 8;
	pm->buffer_size = buffer_size;
	pm->red_size = red_size;
	pm->green_size = green_size;
	pm->blue_size = blue_size;
	pm->alpha_size = alpha_size;
	pm->luminance_size = luminance_size;
	pm->flags = FBDEV_PIXMAP_SUPPORTS_UMP;
	pm->format = 0;
	pm->width = width;
	pm->height = height;

	id = (ump_handle)cedrus_mem_get_ump_handle(mem);
	ump_reference_add(id);
	pm->data = (short unsigned int *)id;
}

static vdpauSurfaceNV register_surface(int isOutputSurface,
				 const void *vdpSurface,
				 uint32_t target,
				 GLsizei numTextureNames,
				 const uint *textureNames)
{
	int i;
	EGLint eglImgAttrs[3];
	vdpauSurfaceNV surfaceNV;

	if (eglSharedContext == EGL_NO_CONTEXT)
	{
		VDPAU_DBG("INTEROP: glVDPAUInitNV failed, no shared context");
		return 0;
	}

	if (!ctx_vdpDevice || !ctx_vdpGetProcAddress)
	{
		VDPAU_DBG("INTEROP: Error ctx");
		return 0;
	}

	if (target != GL_TEXTURE_2D)
	{
		VDPAU_DBG("INTEROP: Error GL_TEXTURE_2D");
		return 0;
	}

	smart nv_surface_ctx_t *nv = handle_alloc(sizeof(*nv), cleanup_surface_nv);
	if (!nv)
	{
		VDPAU_DBG("INTEROP: Error allocating NV handle");
		return 0;
	}

	if (isOutputSurface)
	{
		smart output_surface_ctx_t *vdpsurface = (output_surface_ctx_t *)handle_get((uint32_t)vdpSurface);
		if (!vdpsurface)
		{
			VDPAU_DBG("INTEROP: Error getting output surface handle");
			return 0;
		}

		if (vdpsurface->nv_state == NV_REGISTERED || vdpsurface->nv_state == NV_MAPPED)
		{
			VDPAU_DBG("INTEROP: Error: NV surface already registered or mapped!");
			return 0;
		}

		if ((vdpsurface->rgba.format != VDP_RGBA_FORMAT_B8G8R8A8) &&
		    (vdpsurface->rgba.format != VDP_RGBA_FORMAT_R8G8B8A8))
		{
			VDPAU_DBG("INTEROP: Error format: %d", vdpsurface->rgba.format);
			return 0;
		}

		nv->numTextureNames = numTextureNames;
		memset(nv->textureNames, 0, sizeof(nv->textureNames));
		memcpy(nv->textureNames, textureNames, sizeof(uint) * numTextureNames);
		nv->target = target;
		nv->type = NV_SURFACE_RGBA;
		nv->vdpsurface = sref(vdpsurface);

		vdp_eglAcquireContext();

		/* numTextureNames == 1 for output surfaces */
		for (i = 0; i < nv->numTextureNames; i++)
		{
			GL_CHECK(glBindTexture(GL_TEXTURE_2D, nv->textureNames[i]));
			GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST));
			GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST));
			GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
			GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));

			createNativePixmap(&nv->pixmap[i], nv, (void *)vdpsurface, argb_plane, 1);

			eglImgAttrs[0] = EGL_IMAGE_PRESERVED_KHR;
			eglImgAttrs[1] = EGL_TRUE;
			eglImgAttrs[2] = EGL_NONE;
			EGL_CHECK(nv->eglImage[i] = peglCreateImageKHR(eglDisplay,
							 EGL_NO_CONTEXT,
							 EGL_NATIVE_PIXMAP_KHR,
							 (EGLClientBuffer)&nv->pixmap[i],
							 eglImgAttrs));
			EGL_CHECK(pglEGLImageTargetTexture2DOES(GL_TEXTURE_2D, (GLeglImageOES)nv->eglImage[i]));

			GL_CHECK(glBindTexture(GL_TEXTURE_2D, 0));
		}

		vdp_eglReleaseContext();

		vdpsurface->nv_state = NV_REGISTERED;
	}
	else
	{

		smart video_surface_ctx_t *vdpsurface = (video_surface_ctx_t *)handle_get((uint32_t)vdpSurface);
		if (!vdpsurface)
		{
			VDPAU_DBG("INTEROP: Error getting video surface handle");
			return 0;
		}

		if (vdpsurface->nv_state == NV_REGISTERED || vdpsurface->nv_state == NV_MAPPED)
		{
			VDPAU_DBG("INTEROP: Error: NV surface already registered or mapped!");
			return 0;
		}

		if (vdpsurface->chroma_type != VDP_CHROMA_TYPE_420)
		{
			VDPAU_DBG("INTEROP: Chroma type not supported: %d", vdpsurface->chroma_type);
			return 0;
		}

		nv->numTextureNames = numTextureNames;
		memset(nv->textureNames, 0, sizeof(nv->textureNames));
		memcpy(nv->textureNames, textureNames, sizeof(uint) * numTextureNames);
		nv->target = target;
		nv->type = NV_SURFACE_VIDEO;
		nv->vdpsurface = sref(vdpsurface);

		nv->yuvY = cedrus_mem_alloc(vdpsurface->device->cedrus, vdpsurface->luma_size);
		nv->yuvUV = cedrus_mem_alloc(vdpsurface->device->cedrus, vdpsurface->chroma_size);
		nv->conv_width = (vdpsurface->width + 15) & ~15;
		nv->conv_height = (vdpsurface->height + 15) & ~15;

		vdp_eglAcquireContext();

		/* numTextureNames == 4 for video surfaces */
		for (i = 0; i < nv->numTextureNames; i++)
		{
			GL_CHECK(glBindTexture(GL_TEXTURE_2D, nv->textureNames[i]));
			GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST));
			GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST));
			GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
			GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));

			if (i == 0 || i == 1) /* 0 = top-field luma, 1 = bottom-field luma */
				createNativePixmap(&nv->pixmap[i], nv, (void *)vdpsurface, y_plane, 1);
			else if (i == 2 || i == 3) /* 2 = top-field chroma, 3 = bottom-field chroma */
				createNativePixmap(&nv->pixmap[i], nv, (void *)vdpsurface, uv_plane, 1);

			if (eglSharedContext != EGL_NO_CONTEXT)
			{
				eglImgAttrs[0] = EGL_IMAGE_PRESERVED_KHR;
				eglImgAttrs[1] = EGL_TRUE;
				eglImgAttrs[2] = EGL_NONE;
				EGL_CHECK(nv->eglImage[i] = peglCreateImageKHR(eglDisplay,
								 EGL_NO_CONTEXT,
								 EGL_NATIVE_PIXMAP_KHR,
								 (EGLClientBuffer)&nv->pixmap[i],
								 eglImgAttrs));
				EGL_CHECK(pglEGLImageTargetTexture2DOES(GL_TEXTURE_2D, (GLeglImageOES)nv->eglImage[i]));
			}
		}
		vdpsurface->nv_state = NV_REGISTERED;

		vdp_eglReleaseContext();

	}
	nv->state = NV_REGISTERED;

	if (handle_create((VdpHandle *)&surfaceNV, nv))
	{
		VDPAU_DBG("INTEROP: Error creating surfaceNV handle");
		return 0;
	}

	VDPAU_DBG("INTEROP: surfaceNV registered (%d, 0x%x)", (int)surfaceNV, (unsigned int)&surfaceNV);
	return surfaceNV;
}

vdpauSurfaceNV glVDPAURegisterVideoSurfaceNV(const void *vdpSurface,
					     uint32_t target,
					     GLsizei numTextureNames,
					     const uint *textureNames)
{
	if (numTextureNames > 4)
	{
		VDPAU_DBG("INTEROP: Error OutputSurfaceNV numTextureNames");
		return 0;
	}

	return register_surface(0, vdpSurface, target, numTextureNames, textureNames);
}

vdpauSurfaceNV glVDPAURegisterOutputSurfaceNV(const void *vdpSurface,
					      uint32_t target,
					      GLsizei numTextureNames,
					      const uint *textureNames)
{
	if (numTextureNames != 1)
	{
		VDPAU_DBG("INTEROP: Error OutputSurfaceNV numTextureNames");
		return 0;
	}

	return register_surface(1, vdpSurface, target, numTextureNames, textureNames);
}

int glVDPAUIsSurfaceNV(vdpauSurfaceNV surface)
{
	if (!ctx_vdpDevice || !ctx_vdpGetProcAddress)
	{
		VDPAU_DBG("INTEROP: Error ctx");
		return 0;
	}

	smart nv_surface_ctx_t *nv = handle_get(surface);
	if (!nv)
		return 0;

	return 1;
}

void glVDPAUUnregisterSurfaceNV(vdpauSurfaceNV surface)
{
	int i;

	if (!ctx_vdpDevice || !ctx_vdpGetProcAddress)
	{
		VDPAU_DBG("INTEROP: Error ctx");
		return;
	}

	smart nv_surface_ctx_t *nv = handle_get(surface);
	if (!nv)
	{
		VDPAU_DBG("INTEROP: Error getting surfaceNV handle");
		return;
	}

	if (eglSharedContext == EGL_NO_CONTEXT)
	{
		VDPAU_DBG("INTEROP: glVDPAUInitNV failed, no shared context");
		return;
	}

	if (nv->type == NV_SURFACE_RGBA)
	{
		output_surface_ctx_t *vdpsurface = (output_surface_ctx_t *)nv->vdpsurface;
		if (vdpsurface->nv_state == NV_MAPPED)
		{
			vdpauSurfaceNV surf[] = { surface };
			glVDPAUUnmapSurfacesNV(1, surf);
		}

		vdp_eglAcquireContext();

		/* numTextureNames == 1 for output surfaces */
		for (i = 0; i < nv->numTextureNames; i++)
		{
			if (eglSharedContext != EGL_NO_CONTEXT)
			{
				if (nv->eglImage[i])
				{
					EGL_CHECK(peglDestroyImageKHR(eglDisplay, nv->eglImage[i]));
					nv->eglImage[i] = 0;
				}
			}
			ump_reference_release(nv->pixmap[i].data);
		}

		vdp_eglReleaseContext();

		vdpsurface->nv_state = NV_UNREGISTERED;
		sfree(vdpsurface);
		vdpsurface = 0;

		handle_destroy(surface); // really?
	}
	else
	{

		video_surface_ctx_t *vdpsurface = (video_surface_ctx_t *)nv->vdpsurface;

		if (vdpsurface->nv_state == NV_MAPPED)
		{
			vdpauSurfaceNV surf[] = { surface };
			glVDPAUUnmapSurfacesNV(1, surf);
		}

		vdp_eglAcquireContext();

		/* numTextureNames == 4 for output surfaces */
		for (i = 0; i < nv->numTextureNames; i++)
		{
			if (eglSharedContext != EGL_NO_CONTEXT)
			{
				if (nv->eglImage[i])
				{
					EGL_CHECK(peglDestroyImageKHR(eglDisplay, nv->eglImage[i]));
					nv->eglImage[i] = 0;
				}
			}
			ump_reference_release(nv->pixmap[i].data);
		}

		vdp_eglReleaseContext();

		cedrus_mem_free(nv->yuvY);
		cedrus_mem_free(nv->yuvUV);

		vdpsurface->nv_state = NV_UNREGISTERED;
		sfree(vdpsurface);
		vdpsurface = 0;

		handle_destroy(surface); // really?
	}
	VDPAU_DBG("INTEROP: surfaceNV unregistered");
}

void glVDPAUGetSurfaceivNV(vdpauSurfaceNV surface,
			   uint32_t pname,
			   GLsizei bufSize,
			   GLsizei *length,
			   int *values)
{
	if (!ctx_vdpDevice || !ctx_vdpGetProcAddress)
	{
		VDPAU_DBG("INTEROP: Error ctx");
		return;
	}

	if (pname != SURFACE_STATE_NV)
	{
		VDPAU_DBG("INTEROP: Error pname");
		return;
	}

	if (bufSize < 1)
	{
		VDPAU_DBG("INTEROP: Error bufsize");
		return;
	}

	smart nv_surface_ctx_t *nv = handle_get(surface);
	if (!nv)
	{
		VDPAU_DBG("INTEROP: Error getting surfaceNV handle");
		return;
	}

	switch (nv->state)
	{
		case NV_REGISTERED:
			values[0] = SURFACE_REGISTERED_NV;
			if (length != NULL)
				*length = 1;
			break;
		case NV_MAPPED:
			values[0] = SURFACE_MAPPED_NV;
			if (length != NULL)
				*length = 1;
			break;
		case NV_UNREGISTERED:
		default:
			break;
	}
}

static void setNVAccessValue(void *vdpsurface, int access, int type)
{
	if (type == NV_SURFACE_RGBA)
	{
		output_surface_ctx_t *surface = (output_surface_ctx_t *)vdpsurface;
		surface->nv_access = access;
	}
	else
	{
		video_surface_ctx_t *surface = (video_surface_ctx_t *)vdpsurface;
		surface->nv_access = access;
	}
}

void glVDPAUSurfaceAccessNV(vdpauSurfaceNV surface,
			    uint32_t access)
{
	if (!ctx_vdpDevice || !ctx_vdpGetProcAddress)
	{
		VDPAU_DBG("INTEROP: Error ctx");
		return;
	}

	if (access != READ_ONLY &&
	    access != WRITE_DISCARD_NV &&
	    access != READ_WRITE)
	{
		VDPAU_DBG("INTEROP: Error access param");
		return;
	}

	smart nv_surface_ctx_t *nv = handle_get(surface);
	if (!nv)
	{
		VDPAU_DBG("INTEROP: Error getting surfaceNV handle");
		return;
	}

	if (nv->state == NV_MAPPED)
	{
		VDPAU_DBG("INTEROP: Error nv already mapped");
		return;
	}

	switch (access)
	{
		case READ_ONLY:
			nv->access = NV_READ_ONLY;
			setNVAccessValue(nv->vdpsurface, NV_READ_ONLY, nv->type);
			break;
		case READ_WRITE:
			nv->access = NV_READ_WRITE;
			setNVAccessValue(nv->vdpsurface, NV_READ_WRITE, nv->type);
			break;
		case WRITE_DISCARD_NV:
			nv->access = NV_WRITE_DISCARD_NV;
			setNVAccessValue(nv->vdpsurface, NV_WRITE_DISCARD_NV, nv->type);
			break;
	}
}

void glVDPAUMapSurfacesNV(GLsizei numSurfaces,
			  const vdpauSurfaceNV *surfaces)
{
	int i;

	if (!ctx_vdpDevice || !ctx_vdpGetProcAddress)
	{
		VDPAU_DBG("INTEROP: Error ctx");
		return;
	}

	for (i = 0; i < numSurfaces; i++)
	{
		smart nv_surface_ctx_t *nv = handle_get(surfaces[i]);
		if (!nv)
		{
			VDPAU_DBG("INTEROP: Error getting handle (map)");
			return;
		}

		if (nv->state == NV_MAPPED || nv->state != NV_REGISTERED)
		{
			VDPAU_DBG("INTEROP: Error nv not in registered state");
			return;
		}

		/* Some manipulations that have to be done on the pixels
		 * depending on how the surface wants to be accessed. */
		if (nv->type == NV_SURFACE_RGBA)
		{
			output_surface_ctx_t *vdpsurface = (output_surface_ctx_t *)nv->vdpsurface;

			if (nv->access == NV_WRITE_DISCARD_NV)
			{
				/* Clear surface (in-place), because we only want to write from to it */
				memset(cedrus_mem_get_pointer(vdpsurface->rgba.data), 0, vdpsurface->rgba.width * vdpsurface->rgba.height * 4);
			}

			vdpsurface->nv_state = NV_MAPPED;
		}
		else if (nv->type == NV_SURFACE_VIDEO)
		{
			video_surface_ctx_t *vdpsurface = (video_surface_ctx_t *)nv->vdpsurface;

			if (nv->access == NV_WRITE_DISCARD_NV)
			{
				/* Clear surface, because we only want to write from to it. */
				memset(cedrus_mem_get_pointer(vdpsurface->yuv->data), 0, (vdpsurface->luma_size + vdpsurface->chroma_size));
			}

			if (nv->access == NV_READ_ONLY || nv->access == NV_READ_WRITE)
			{
				/* Convert the tiled yuv format and process it into the nv->yuvY and nv->yuvUV buffer, which are connected to the eglImage,
				 * so that the application can use it. */
				tiled_to_planar(cedrus_mem_get_pointer(vdpsurface->yuv->data), nv->yuvY, nv->conv_width, vdpsurface->width, vdpsurface->height);
				tiled_to_planar(cedrus_mem_get_pointer(vdpsurface->yuv->data) + vdpsurface->luma_size, nv->yuvUV, nv->conv_width, vdpsurface->width, vdpsurface->height / 2);
			}

			vdpsurface->nv_state = NV_MAPPED;
		}
		nv->state = NV_MAPPED;
	}
}

void glVDPAUUnmapSurfacesNV(GLsizei numSurfaces,
			    const vdpauSurfaceNV *surfaces)
{
	int i;
#ifdef GLFILE
	static int l;
	FILE *fp;
#endif
	if (!ctx_vdpDevice || !ctx_vdpGetProcAddress)
	{
		VDPAU_DBG("INTEROP: Error ctx");
		return;
	}

	for (i = 0; i < numSurfaces; i++)
	{
		smart nv_surface_ctx_t *nv = handle_get(surfaces[i]);
		if (!nv)
		{
			VDPAU_DBG("INTEROP: Error getting handle (unmap)");
			return;
		}

		if (nv->state != NV_MAPPED)
		{
			VDPAU_DBG("INTEROP: Error nv not in mapped state");
			return;
		}

		if (nv->type == NV_SURFACE_RGBA)
		{
			output_surface_ctx_t *vdpsurface = (output_surface_ctx_t *)nv->vdpsurface;

			if (nv->access == NV_READ_ONLY)
			{
			}

			vdpsurface->nv_state = NV_REGISTERED;
#ifdef GLFILE
			// Raw pixel buffer output for debugging
			char filename[sizeof("/srv/public/unmap999.rgba")];
			sprintf(filename, "/srv/public/unmap%03d.rgba", l);
			l++;
			fp = fopen(filename, "w+");
			fwrite(cedrus_mem_get_pointer(vdpsurface->rgba.data), 4, vdpsurface->rgba.width * vdpsurface->rgba.height, fp);
			fclose(fp);
#endif
			vdpsurface->rgba.id++;
			vdpsurface->rgba.gl = 1;
		}
		else if (nv->type == NV_SURFACE_VIDEO)
		{
			video_surface_ctx_t *vdpsurface = (video_surface_ctx_t *)nv->vdpsurface;

			if (nv->access == NV_WRITE_DISCARD_NV || nv->access == NV_READ_WRITE)
				vdpsurface->source_format = VDP_YCBCR_FORMAT_NV12;

			vdpsurface->nv_state = NV_REGISTERED;
		}
		nv->state = NV_REGISTERED;
	}
}
