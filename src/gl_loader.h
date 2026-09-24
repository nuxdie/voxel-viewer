// Tiny OpenGL 3.3 core function loader (avoids a GLEW/glad dependency).
#pragma once

#include <GL/glcorearb.h>

#define VV_GL_FUNCTIONS(X)                                                         \
    X(PFNGLCLEARPROC, glClear)                                                     \
    X(PFNGLCLEARCOLORPROC, glClearColor)                                           \
    X(PFNGLENABLEPROC, glEnable)                                                   \
    X(PFNGLDISABLEPROC, glDisable)                                                 \
    X(PFNGLVIEWPORTPROC, glViewport)                                               \
    X(PFNGLBLENDFUNCPROC, glBlendFunc)                                             \
    X(PFNGLBLENDFUNCSEPARATEPROC, glBlendFuncSeparate)                             \
    X(PFNGLDEPTHMASKPROC, glDepthMask)                                             \
    X(PFNGLDEPTHFUNCPROC, glDepthFunc)                                             \
    X(PFNGLCULLFACEPROC, glCullFace)                                               \
    X(PFNGLPOLYGONMODEPROC, glPolygonMode)                                         \
    X(PFNGLPOLYGONOFFSETPROC, glPolygonOffset)                                     \
    X(PFNGLREADPIXELSPROC, glReadPixels)                                           \
    X(PFNGLREADBUFFERPROC, glReadBuffer)                                           \
    X(PFNGLPIXELSTOREIPROC, glPixelStorei)                                         \
    X(PFNGLGETSTRINGPROC, glGetString)                                             \
    X(PFNGLGETERRORPROC, glGetError)                                               \
    X(PFNGLFINISHPROC, glFinish)                                                   \
    X(PFNGLDRAWARRAYSPROC, glDrawArrays)                                           \
    X(PFNGLDRAWELEMENTSPROC, glDrawElements)                                       \
    X(PFNGLCREATESHADERPROC, glCreateShader)                                       \
    X(PFNGLSHADERSOURCEPROC, glShaderSource)                                       \
    X(PFNGLCOMPILESHADERPROC, glCompileShader)                                     \
    X(PFNGLGETSHADERIVPROC, glGetShaderiv)                                         \
    X(PFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog)                               \
    X(PFNGLDELETESHADERPROC, glDeleteShader)                                       \
    X(PFNGLCREATEPROGRAMPROC, glCreateProgram)                                     \
    X(PFNGLATTACHSHADERPROC, glAttachShader)                                       \
    X(PFNGLLINKPROGRAMPROC, glLinkProgram)                                         \
    X(PFNGLGETPROGRAMIVPROC, glGetProgramiv)                                       \
    X(PFNGLGETPROGRAMINFOLOGPROC, glGetProgramInfoLog)                             \
    X(PFNGLDELETEPROGRAMPROC, glDeleteProgram)                                     \
    X(PFNGLUSEPROGRAMPROC, glUseProgram)                                           \
    X(PFNGLGETUNIFORMLOCATIONPROC, glGetUniformLocation)                           \
    X(PFNGLUNIFORM1IPROC, glUniform1i)                                             \
    X(PFNGLUNIFORM1FPROC, glUniform1f)                                             \
    X(PFNGLUNIFORM3FPROC, glUniform3f)                                             \
    X(PFNGLUNIFORM3FVPROC, glUniform3fv)                                           \
    X(PFNGLUNIFORM4FVPROC, glUniform4fv)                                           \
    X(PFNGLUNIFORMMATRIX4FVPROC, glUniformMatrix4fv)                               \
    X(PFNGLGENVERTEXARRAYSPROC, glGenVertexArrays)                                 \
    X(PFNGLBINDVERTEXARRAYPROC, glBindVertexArray)                                 \
    X(PFNGLDELETEVERTEXARRAYSPROC, glDeleteVertexArrays)                           \
    X(PFNGLGENBUFFERSPROC, glGenBuffers)                                           \
    X(PFNGLBINDBUFFERPROC, glBindBuffer)                                           \
    X(PFNGLBUFFERDATAPROC, glBufferData)                                           \
    X(PFNGLDELETEBUFFERSPROC, glDeleteBuffers)                                     \
    X(PFNGLVERTEXATTRIBPOINTERPROC, glVertexAttribPointer)                         \
    X(PFNGLVERTEXATTRIBIPOINTERPROC, glVertexAttribIPointer)                       \
    X(PFNGLENABLEVERTEXATTRIBARRAYPROC, glEnableVertexAttribArray)

#define VV_DECLARE(type, name) extern type name;
namespace gl {
VV_GL_FUNCTIONS(VV_DECLARE)
// Loads all functions via the given GetProcAddress; returns false if any are missing.
bool load(void* (*getProc)(const char*));
}  // namespace gl
#undef VV_DECLARE

using namespace gl;
