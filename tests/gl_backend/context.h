/* The context every test here runs in: OpenGL 3.3 core on a surfaceless EGL
 * display (Mesa's llvmpipe will do), made current. 0, having said why, when
 * there is none. */
#ifndef GL_BACKEND_CONTEXT_H
#define GL_BACKEND_CONTEXT_H

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <stdio.h>

static int gl_backend_context(void)
{
    static const EGLint cfga[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 24, EGL_NONE };
    static const EGLint pba[] = { EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE };
    static const EGLint ctxa[] = {
        EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 3,
        EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
        EGL_NONE };
    EGLDisplay dpy;
    EGLConfig cfg;
    EGLSurface surf;
    EGLContext ctx;
    EGLint n;

    dpy = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
    if (dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, NULL, NULL)) {
        fprintf(stderr, "FAIL no EGL display\n");
        return 0;
    }
    eglBindAPI(EGL_OPENGL_API);
    if (!eglChooseConfig(dpy, cfga, &cfg, 1, &n) || n < 1) {
        fprintf(stderr, "FAIL no EGL config\n");
        return 0;
    }
    surf = eglCreatePbufferSurface(dpy, cfg, pba);
    ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctxa);
    if (ctx == EGL_NO_CONTEXT || !eglMakeCurrent(dpy, surf, surf, ctx)) {
        fprintf(stderr, "FAIL no OpenGL 3.3 core context (0x%x)\n", eglGetError());
        return 0;
    }
    printf("%s | %s\n", (const char *)glGetString(GL_VERSION),
           (const char *)glGetString(GL_RENDERER));
    return 1;
}

#endif
