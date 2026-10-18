# WebGL for the Qt port renders through ANGLE's D3D11 backend.
#
# This mirrors PlatformWin.cmake, with one deliberate difference: the libraries stay STATIC.
# The reader ships as a single self-contained .exe, so a libGLESv2.dll next to it is not an
# option. Without this file at all, CMake silently skips the whole platform configuration -
# the D3D sources at CMakeLists.txt:91 are added under a plain if (WIN32) and get compiled
# anyway, without ANGLE_ENABLE_D3D11 or the Windows headers, and fail on an undeclared UINT.

list(APPEND ANGLE_DEFINITIONS
    # The Khronos headers declare every entry point __declspec(dllimport) on Windows unless this
    # says otherwise, which is right for PlatformWin's shared libGLESv2 and wrong for ours.
    # KHRONOS_STATIC covers GL_APICALL and EGLAPI together; WebCore needs it too, and gets it
    # from the INTERFACE definitions set in Source/WebCore/PlatformQt.cmake.
    KHRONOS_STATIC
    GL_APICALL=
    GL_API=
    # ANGLE's own entry points (the EGL_*/GL_* names in include/export.h) go through
    # ANGLE_EXPORT, which is dllexport while building the library and dllimport for everyone
    # else. Neither is right for a static library, and the header honours a pre-definition.
    ANGLE_EXPORT=
    NOMINMAX
)

list(APPEND ANGLE_SOURCES
    ${d3d11_backend_sources}
    ${d3d_shared_sources}

    ${angle_translator_hlsl_sources}

    ${libangle_gpu_info_util_sources}
    ${libangle_gpu_info_util_win_sources}
)

list(APPEND ANGLE_DEFINITIONS
    ANGLE_ENABLE_D3D11
    ANGLE_ENABLE_HLSL
)

list(APPEND ANGLEGLESv2_LIBRARIES dxguid dxgi)

# D3D9 support is meant to be optional, but ANGLE does not compile without it.
list(APPEND ANGLE_SOURCES ${d3d9_backend_sources})
list(APPEND ANGLE_DEFINITIONS ANGLE_ENABLE_D3D9)
list(APPEND ANGLEGLESv2_LIBRARIES d3d9)

set(GLESv2_LIBRARY_TYPE STATIC)
set(EGL_LIBRARY_TYPE STATIC)
