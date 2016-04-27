/*
 * Copyright (C) Boudewijn Rempt <boud@valdyas.org>, (C) 2006
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#ifndef KIS_OPENGL_CANVAS_2_P_H
#define KIS_OPENGL_CANVAS_2_P_H

#include <opengl/kis_opengl.h>

/**
 * This is a workaround for a very slow updates in OpenGL canvas (~6ms).
 * The delay happens because of VSync in the swapBuffers() call. At first
 * we try to disable VSync. If it fails we just disable Double Buffer
 * completely.
 *
 * This file is effectively a bit of copy-paste from qgl_x11.cpp
 */

#if defined Q_OS_LINUX

#include <QByteArray>
#include <QVector>
#include <QLibrary>
#include <QX11Info>
#include <QOpenGLContext>
#include <QApplication>

#ifndef GL_NUM_EXTENSIONS
#define GL_NUM_EXTENSIONS 0x821D
#endif

QString gl_library_name() {
#if defined (QT_OPENGL_ES_2)
    return QLatin1String("GLESv2");
#else
    return QLatin1String("GL");
#endif
}

namespace VSyncWorkaround {

    bool tryDisableVSync(QOpenGLContext* ctx) {
        bool result = false;

        bool triedDisable = false;
        Display *dpy = QX11Info::display();
        dbgOpenGL << "OpenGL architecture is" << gl_library_name();

        if (ctx->hasExtension("GLX_EXT_swap_control")) {
            dbgOpenGL << "Swap control extension found.";
            typedef WId (*k_glXGetCurrentDrawable)(void);
            typedef void (*kis_glXSwapIntervalEXT)(Display*, WId, int);
            k_glXGetCurrentDrawable kis_glXGetCurrentDrawable = (k_glXGetCurrentDrawable)ctx->getProcAddress("glXGetCurrentDrawable");
            kis_glXSwapIntervalEXT glXSwapIntervalEXT = (kis_glXSwapIntervalEXT)ctx->getProcAddress("glXSwapIntervalEXT");
            WId wid = kis_glXGetCurrentDrawable();

            if (glXSwapIntervalEXT) {
                glXSwapIntervalEXT(dpy, wid, 0);
                triedDisable = true;

                unsigned int swap = 1;

#ifdef GLX_SWAP_INTERVAL_EXT
                typedef int (*k_glXQueryDrawable)(Display *, WId, int, unsigned int *);
                k_glXQueryDrawable kis_glXQueryDrawable = (k_glXQueryDrawable)ctx->getProcAddress("glXQueryDrawable");
                kis_glXQueryDrawable(dpy, wid, GLX_SWAP_INTERVAL_EXT, &swap);
#endif

                result = !swap;
            } else {
                dbgOpenGL << "Couldn't load glXSwapIntervalEXT extension function";
            }
        } else if (ctx->hasExtension("GLX_MESA_swap_control")) {
            dbgOpenGL << "MESA swap control extension found.";
            typedef int (*kis_glXSwapIntervalMESA)(unsigned int);
            typedef int (*kis_glXGetSwapIntervalMESA)(void);

            kis_glXSwapIntervalMESA glXSwapIntervalMESA = (kis_glXSwapIntervalMESA)ctx->getProcAddress("glXSwapIntervalMESA");
            kis_glXGetSwapIntervalMESA glXGetSwapIntervalMESA = (kis_glXGetSwapIntervalMESA)ctx->getProcAddress("glXGetSwapIntervalMESA");

            if (glXSwapIntervalMESA) {
                int retval = glXSwapIntervalMESA(0);
                triedDisable = true;

                int swap = 1;

                if (glXGetSwapIntervalMESA) {
                    swap = glXGetSwapIntervalMESA();
                } else {
                    dbgOpenGL << "Couldn't load glXGetSwapIntervalMESA extension function";
                }

                result = !retval && !swap;
            } else {
                dbgOpenGL << "Couldn't load glXSwapIntervalMESA extension function";
            }
        } else {
            dbgOpenGL << "There is neither GLX_EXT_swap_control or GLX_MESA_swap_control extension supported";
        }

        if (triedDisable && !result) {
            errUI;
            errUI << "CRITICAL: Your video driver forbids disabling VSync!";
            errUI << "CRITICAL: Try toggling some VSync- or VBlank-related options in your driver configuration dialog.";
            errUI << "CRITICAL: NVIDIA users can do:";
            errUI << "CRITICAL: sudo nvidia-settings  >  (tab) OpenGL settings > Sync to VBlank  ( unchecked )";
            errUI;
        }
        return result;
    }
}

#elif defined Q_OS_WIN
namespace VSyncWorkaround {
    bool tryDisableVSync(QOpenGLContext *ctx) {
        bool retval = false;

        if (ctx->hasExtension("WGL_EXT_swap_control")) {
            typedef void (*wglSwapIntervalEXT)(int);
            typedef int  (*wglGetSwapIntervalEXT)(void);
            ((wglSwapIntervalEXT)ctx->getProcAddress("wglSwapIntervalEXT"))(0);
            int interval = ((wglGetSwapIntervalEXT)ctx->getProcAddress("wglGetSwapIntervalEXT"))();

            if (interval) {
                warnOpenGL << "Failed to disable VSync with WGL_EXT_swap_control";
            }

            retval = !interval;
        } else {
            warnOpenGL << "WGL_EXT_swap_control extension is not available. Found extensions" << ctx->extensions();
        }
        return retval;
    }
}

#else  // !defined Q_OS_LINUX && !defined Q_OS_WIN

namespace VSyncWorkaround {
  bool tryDisableVSync(QOpenGLContext *) {
        return false;
    }
}
#endif // defined Q_OS_LINUX

namespace Sync {
    //For checking sync status
    enum SyncStatus {
        Signaled,
        Unsignaled
    };

#ifndef GL_SYNC_GPU_COMMANDS_COMPLETE
    #define GL_SYNC_GPU_COMMANDS_COMPLETE 0x9117
#endif
#ifndef GL_UNSIGNALED
    #define GL_UNSIGNALED 0x9118
#endif
#ifndef GL_SIGNALED
    #define GL_SIGNALED 0x9119
#endif
#ifndef GL_SYNC_STATUS
    #define GL_SYNC_STATUS 0x9114
#endif

    //Function pointers for glFenceSync and glGetSynciv
    typedef GLsync (*kis_glFenceSync)(GLenum, GLbitfield);
    static kis_glFenceSync k_glFenceSync = 0;
    typedef void (*kis_glGetSynciv)(GLsync, GLenum, GLsizei, GLsizei*, GLint*);
    static kis_glGetSynciv k_glGetSynciv = 0;
    typedef void (*kis_glDeleteSync)(GLsync);
    static kis_glDeleteSync k_glDeleteSync = 0;
    typedef GLenum (*kis_glClientWaitSync)(GLsync,GLbitfield,GLuint64);
    static kis_glClientWaitSync k_glClientWaitSync = 0;

    //Initialise the function pointers for glFenceSync and glGetSynciv
    //Note: Assumes a current OpenGL context.
    void init(QOpenGLContext* ctx) {
#if defined Q_OS_WIN
        if (KisOpenGL::supportsFenceSync()) {
#ifdef ENV64BIT
            k_glFenceSync  = (kis_glFenceSync)ctx->getProcAddress("glFenceSync");
            k_glGetSynciv  = (kis_glGetSynciv)ctx->getProcAddress("glGetSynciv");
            k_glDeleteSync = (kis_glDeleteSync)ctx->getProcAddress("glDeleteSync");
#else
            k_glFenceSync  = (kis_glFenceSync)ctx->getProcAddress("glFenceSyncARB");
            k_glGetSynciv  = (kis_glGetSynciv)ctx->getProcAddress("glGetSyncivARB");
            k_glDeleteSync = (kis_glDeleteSync)ctx->getProcAddress("glDeleteSyncARB");
#endif
            k_glClientWaitSync = (kis_glClientWaitSync)ctx->getProcAddress("glClientWaitSync");
        }
#elif defined Q_OS_LINUX
        if (KisOpenGL::supportsFenceSync()) {
            k_glFenceSync  = (kis_glFenceSync)ctx->getProcAddress("glFenceSync");
            k_glGetSynciv  = (kis_glGetSynciv)ctx->getProcAddress("glGetSynciv");
            k_glDeleteSync = (kis_glDeleteSync)ctx->getProcAddress("glDeleteSync");
            k_glClientWaitSync = (kis_glClientWaitSync)ctx->getProcAddress("glClientWaitSync");
        }
#elif defined Q_OS_MAC
        dbgOpenGL << "check fence sync support" << KisOpenGL::supportsFenceSync();
        if (KisOpenGL::supportsFenceSync()) {
            k_glFenceSync  = (kis_glFenceSync)ctx->getProcAddress("glFenceSync");
            k_glGetSynciv  = (kis_glGetSynciv)ctx->getProcAddress("glGetSynciv");
            k_glDeleteSync = (kis_glDeleteSync)ctx->getProcAddress("glDeleteSync");
            k_glClientWaitSync = (kis_glClientWaitSync)ctx->getProcAddress("glClientWaitSync");
        }
#endif
        if (k_glFenceSync  == 0 || k_glGetSynciv      == 0 ||
            k_glDeleteSync == 0 || k_glClientWaitSync == 0) {
            warnOpenGL << "Could not find sync functions, disabling sync notification.";
        }
    }

    //Get a fence sync object from OpenGL
    GLsync getSync() {
        if(k_glFenceSync) {
            GLsync sync = k_glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
            if (KisOpenGL::needsFenceWorkaround()) {
                k_glClientWaitSync(sync, 0, 1);
            }
            return sync;
        }
        return 0;
    }

    //Check the status of a sync object
    SyncStatus syncStatus(GLsync syncObject) {
        if(syncObject && k_glGetSynciv) {
            GLint status = -1;
            k_glGetSynciv(syncObject, GL_SYNC_STATUS, 1, 0, &status);
            return status == GL_SIGNALED ? Sync::Signaled : Sync::Unsignaled;
        }
        return Sync::Signaled;
    }

    void deleteSync(GLsync syncObject) {
        if(syncObject && k_glDeleteSync) {
            k_glDeleteSync(syncObject);
        }
    }
}
#endif // KIS_OPENGL_CANVAS_2_P_H
