# 32-bit x86, fully static, static CRT (/MT) - matches the static Qt 5.14.2 bundle.
# Toolset intentionally unset: use the default (v143). The tree requires C++20
# (Source/cmake/OptionsCommon.cmake), which v141 cannot provide.
set(VCPKG_TARGET_ARCHITECTURE x86)
set(VCPKG_CRT_LINKAGE static)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_BUILD_TYPE release)
