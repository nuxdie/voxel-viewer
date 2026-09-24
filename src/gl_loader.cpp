#include "gl_loader.h"

#include <cstdio>

namespace gl {

#define VV_DEFINE(type, name) type name = nullptr;
VV_GL_FUNCTIONS(VV_DEFINE)
#undef VV_DEFINE

bool load(void* (*getProc)(const char*)) {
    bool ok = true;
#define VV_LOAD(type, name)                                          \
    name = reinterpret_cast<type>(getProc(#name));                   \
    if (!name) {                                                     \
        std::fprintf(stderr, "OpenGL function missing: %s\n", #name); \
        ok = false;                                                  \
    }
    VV_GL_FUNCTIONS(VV_LOAD)
#undef VV_LOAD
    return ok;
}

}  // namespace gl
