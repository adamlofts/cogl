/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2011 Intel Corporation.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 *
 * Authors:
 *   Robert Bragg <robert@linux.intel.com>
 *   Neil Roberts <neil@linux.intel.com>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "bcm_host.h"

#include "cogl-winsys-egl-null-private.h"
#include "cogl-winsys-egl-private.h"
#include "cogl-renderer-private.h"
#include "cogl-framebuffer-private.h"
#include "cogl-onscreen-private.h"

static const CoglWinsysEGLVtable _cogl_winsys_egl_vtable;

typedef struct _CoglDisplayNull
{
  int egl_surface_width;
  int egl_surface_height;
  CoglBool have_onscreen;
} CoglDisplayNull;

static void
_cogl_winsys_renderer_disconnect (CoglRenderer *renderer)
{
  CoglRendererEGL *egl_renderer = renderer->winsys;

  eglTerminate (egl_renderer->edpy);

  g_slice_free (CoglRendererEGL, egl_renderer);
}

static CoglBool
_cogl_winsys_renderer_connect (CoglRenderer *renderer,
                               CoglError **error)
{
  CoglRendererEGL *egl_renderer;

  renderer->winsys = g_slice_new0 (CoglRendererEGL);
  egl_renderer = renderer->winsys;

  egl_renderer->platform_vtable = &_cogl_winsys_egl_vtable;

  egl_renderer->edpy = eglGetDisplay (EGL_DEFAULT_DISPLAY);

  if (!_cogl_winsys_egl_renderer_connect_common (renderer, error))
    goto error;

  return TRUE;

error:
  _cogl_winsys_renderer_disconnect (renderer);
  return FALSE;
}


static CoglBool
_cogl_winsys_egl_context_created (CoglDisplay *display,
                                  CoglError **error)
{
  CoglRenderer *renderer = display->renderer;
  CoglRendererEGL *egl_renderer = renderer->winsys;
  CoglDisplayEGL *egl_display = display->winsys;
  CoglDisplayNull *null_display = egl_display->platform;
  const char *error_message;

  DISPMANX_ELEMENT_HANDLE_T dispman_element;
  DISPMANX_DISPLAY_HANDLE_T dispman_display;
  DISPMANX_UPDATE_HANDLE_T dispman_update;
  VC_RECT_T dst_rect;
  VC_RECT_T src_rect;

  uint32_t screen_width;
  uint32_t screen_height;
  int success = graphics_get_display_size(0 /* LCD */, &screen_width, &screen_height);
  if (success < 0) {
    error_message = "graphics_get_display_size() failed";
    goto fail;
  }

   dst_rect.x = 0;
   dst_rect.y = 0;
   dst_rect.width = screen_width;
   dst_rect.height = screen_height;
      
   src_rect.x = 0;
   src_rect.y = 0;
   src_rect.width = screen_width << 16;
   src_rect.height = screen_height << 16;        

   dispman_display = vc_dispmanx_display_open( 0 /* LCD */);
   dispman_update = vc_dispmanx_update_start( 0 );
         
   dispman_element = vc_dispmanx_element_add ( dispman_update, dispman_display,
      0/*layer*/, &dst_rect, 0/*src*/,
      &src_rect, DISPMANX_PROTECTION_NONE, 0 /*alpha*/, 0/*clamp*/, 0/*transform*/);

   static EGL_DISPMANX_WINDOW_T nativewindow;
   nativewindow.element = dispman_element;
   nativewindow.width = screen_width;
   nativewindow.height = screen_height;
   vc_dispmanx_update_submit_sync( dispman_update );
      
  egl_display->egl_surface =
    eglCreateWindowSurface (egl_renderer->edpy,
                            egl_display->egl_config,
                            &nativewindow,
                            NULL);
  if (egl_display->egl_surface == EGL_NO_SURFACE)
    {
      error_message = "Unable to create EGL window surface";
      goto fail;
    }

  if (!_cogl_winsys_egl_make_current (display,
                                      egl_display->egl_surface,
                                      egl_display->egl_surface,
                                      egl_display->egl_context))
    {
      error_message = "Unable to eglMakeCurrent with egl surface";
      goto fail;
    }

  null_display->egl_surface_width = screen_width;
  null_display->egl_surface_height = screen_height;

/*
  eglQuerySurface (egl_renderer->edpy,
                   egl_display->egl_surface,
                   EGL_WIDTH,
                   &null_display->egl_surface_width);

  eglQuerySurface (egl_renderer->edpy,
                   egl_display->egl_surface,
                   EGL_HEIGHT,
                   &null_display->egl_surface_height);
*/

  return TRUE;

 fail:
  _cogl_set_error (error, COGL_WINSYS_ERROR,
               COGL_WINSYS_ERROR_CREATE_CONTEXT,
               "%s", error_message);
  return FALSE;
}

static CoglBool
_cogl_winsys_egl_display_setup (CoglDisplay *display,
                                CoglError **error)
{
  CoglDisplayEGL *egl_display = display->winsys;
  CoglDisplayNull *null_display;

  null_display = g_slice_new0 (CoglDisplayNull);
  egl_display->platform = null_display;

  return TRUE;
}

static void
_cogl_winsys_egl_display_destroy (CoglDisplay *display)
{
  CoglDisplayEGL *egl_display = display->winsys;

  g_slice_free (CoglDisplayNull, egl_display->platform);
}

static void
_cogl_winsys_egl_cleanup_context (CoglDisplay *display)
{
  CoglRenderer *renderer = display->renderer;
  CoglRendererEGL *egl_renderer = renderer->winsys;
  CoglDisplayEGL *egl_display = display->winsys;

  if (egl_display->egl_surface != EGL_NO_SURFACE)
    {
      eglDestroySurface (egl_renderer->edpy, egl_display->egl_surface);
      egl_display->egl_surface = EGL_NO_SURFACE;
    }
}

static CoglBool
_cogl_winsys_egl_onscreen_init (CoglOnscreen *onscreen,
                                EGLConfig egl_config,
                                CoglError **error)
{
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (onscreen);
  CoglContext *context = framebuffer->context;
  CoglDisplay *display = context->display;
  CoglDisplayEGL *egl_display = display->winsys;
  CoglDisplayNull *null_display = egl_display->platform;
  CoglOnscreenEGL *egl_onscreen = onscreen->winsys;

  if (null_display->have_onscreen)
    {
      _cogl_set_error (error, COGL_WINSYS_ERROR,
                   COGL_WINSYS_ERROR_CREATE_ONSCREEN,
                   "EGL platform only supports a single onscreen window");
      return FALSE;
    }

  egl_onscreen->egl_surface = egl_display->egl_surface;

  _cogl_framebuffer_winsys_update_size (framebuffer,
                                        null_display->egl_surface_width,
                                        null_display->egl_surface_height);
  null_display->have_onscreen = TRUE;

  return TRUE;
}

static void
_cogl_winsys_egl_onscreen_deinit (CoglOnscreen *onscreen)
{
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (onscreen);
  CoglContext *context = framebuffer->context;
  CoglDisplay *display = context->display;
  CoglDisplayEGL *egl_display = display->winsys;
  CoglDisplayNull *null_display = egl_display->platform;

  null_display->have_onscreen = FALSE;
}

static const CoglWinsysEGLVtable
_cogl_winsys_egl_vtable =
  {
    .display_setup = _cogl_winsys_egl_display_setup,
    .display_destroy = _cogl_winsys_egl_display_destroy,
    .context_created = _cogl_winsys_egl_context_created,
    .cleanup_context = _cogl_winsys_egl_cleanup_context,
    .onscreen_init = _cogl_winsys_egl_onscreen_init,
    .onscreen_deinit = _cogl_winsys_egl_onscreen_deinit
  };

const CoglWinsysVtable *
_cogl_winsys_egl_null_get_vtable (void)
{
  static CoglBool vtable_inited = FALSE;
  static CoglWinsysVtable vtable;

  if (!vtable_inited)
    {
      /* The EGL_NULL winsys is a subclass of the EGL winsys so we
         start by copying its vtable */

      vtable = *_cogl_winsys_egl_get_vtable ();

      vtable.id = COGL_WINSYS_ID_EGL_NULL;
      vtable.name = "EGL_NULL";

      vtable.renderer_connect = _cogl_winsys_renderer_connect;
      vtable.renderer_disconnect = _cogl_winsys_renderer_disconnect;

      vtable_inited = TRUE;
    }

  return &vtable;
}
