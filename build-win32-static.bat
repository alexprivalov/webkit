@echo off
REM ---------------------------------------------------------------------------
REM Static 32-bit x86 QtWebKit build (Qt port) for re_ebook.
REM
REM Deliberately does NOT use Tools/Scripts/build-webkit: its Qt+Windows path
REM unconditionally runs update-qtwebkit-win-libs, which downloads the dead
REM Vitallium/qtwebkit-libs-win 2015 zip and dies on failure with no skip flag,
REM and it forces DEVELOPER_MODE=ON which turns on /WX.
REM
REM Deliberately does NOT use vcpkg's CMAKE_TOOLCHAIN_FILE either: both vcpkg
REM and WebKit (Source/cmake/WebKitFindPackage.cmake) override find_package and
REM delegate to _find_package, so loading both yields mutual recursion and
REM "Maximum recursion depth of 1000 exceeded". vcpkg's installed tree has a
REM standard layout, so putting it on CMAKE_PREFIX_PATH is sufficient.
REM
REM CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded is REQUIRED, not redundant with
REM USE_STATIC_RUNTIME. cmake_minimum_required(3.16) makes CMP0091 NEW, so the CRT
REM flag comes from CMAKE_MSVC_RUNTIME_LIBRARY and no longer appears in
REM CMAKE_CXX_FLAGS -- which is where USE_STATIC_RUNTIME does its /MD -> /MT regex.
REM USE_STATIC_RUNTIME is therefore a no-op on its own, and the build would silently
REM use the dynamic CRT and fail to link against the /MT static Qt.
REM
REM Bitness comes entirely from the vcvarsall environment (Ninja passes no
REM -A flag), so configure and build MUST share one shell environment.
REM
REM Usage: build-win32-static.bat [deps|configure|spike|build|install|bundle]
REM ---------------------------------------------------------------------------
setlocal

if "%VSDIR%"==""    set "VSDIR=C:\Program Files\Microsoft Visual Studio\2022\Community"
if "%SRC%"==""      set "SRC=C:\webkit"
if "%BUILDDIR%"=="" set "BUILDDIR=C:\webkit-build"
if "%PREFIX%"==""   set "PREFIX=C:\webkit-install"
if "%QTDIR%"==""    set "QTDIR=C:/qt5_static"
if "%VCPKGDIR%"=="" set "VCPKGDIR=C:\vcpkg"
if "%TRIPLET%"==""  set "TRIPLET=x86-windows-static"

set "VCPKGINST=%SRC:\=/%/vcpkg_installed/%TRIPLET%"

REM Native cross tools: arm64_x86 on an ARM64 host, amd64_x86 on x64 CI runners.
if /i "%PROCESSOR_ARCHITECTURE%"=="ARM64" (set "HOSTARCH=arm64") else (set "HOSTARCH=amd64")
call "%VSDIR%\VC\Auxiliary\Build\vcvarsall.bat" %HOSTARCH%_x86
if errorlevel 1 exit /b 1

REM WebKitCCache.cmake auto-enables ccache whenever one is on PATH. Strawberry Perl
REM ships C:\Strawberry\c\bin\ccache.exe, so it gets picked up by accident -- it
REM measured a 0.16%% hit rate here (direct mode 0%%) and then deadlocked mid-build,
REM leaving ninja waiting on a ccache process that burned 0.5s CPU in 42 minutes.
REM A reproducible build must not depend on PATH ordering, so opt out explicitly.
set WK_USE_CCACHE=NO

set "CMDIR=%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake"
set "PATH=%CMDIR%\CMake\bin;%CMDIR%\Ninja;%PATH%"

if /i "%1"=="deps"      goto deps
if /i "%1"=="spike"     goto spike
if /i "%1"=="build"     goto build
if /i "%1"=="install"   goto install
if /i "%1"=="bundle"    goto bundle
goto configure

:deps
"%VCPKGDIR%\vcpkg.exe" install --triplet %TRIPLET% ^
  --overlay-triplets=%SRC%\WebKitLibraries\triplets --x-manifest-root=%SRC%
goto :eof

:configure
cmake -S "%SRC%" -B "%BUILDDIR%" -G Ninja ^
  -DPORT=Qt ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_PREFIX_PATH="%QTDIR%;%VCPKGINST%" ^
  -DCMAKE_INSTALL_PREFIX=%PREFIX% ^
  -DDEVELOPER_MODE=OFF ^
  -DUSE_STATIC_RUNTIME=ON ^
  -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded ^
  -DENABLE_WEBKIT2=OFF -DENABLE_TOOLS=OFF -DENABLE_API_TESTS=OFF ^
  -DENABLE_TEST_SUPPORT=OFF ^
  -DENABLE_VIDEO=ON -DUSE_QT_MULTIMEDIA=ON -DENABLE_MODERN_MEDIA_CONTROLS=ON ^
  -DENABLE_WEB_AUDIO=OFF ^
  -DENABLE_XSLT=OFF ^
  -DENABLE_DARK_MODE_CSS=ON -DENABLE_VARIATION_FONTS=ON ^
  -DENABLE_OFFSCREEN_CANVAS=ON ^
  -DENABLE_INPUT_TYPE_DATE=OFF -DENABLE_INPUT_TYPE_TIME=OFF ^
  -DENABLE_INPUT_TYPE_MONTH=OFF -DENABLE_INPUT_TYPE_WEEK=OFF ^
  -DENABLE_INPUT_TYPE_DATETIMELOCAL=OFF ^
  -DENABLE_VIDEO_PRESENTATION_MODE=OFF -DENABLE_MEDIA_CONTROLS_CONTEXT_MENUS=OFF ^
  -DENABLE_GEOLOCATION=OFF -DENABLE_DEVICE_ORIENTATION=OFF ^
  -DENABLE_WEB_CRYPTO=OFF ^
  -DENABLE_PRINT_SUPPORT=OFF -DENABLE_OPENGL=OFF ^
  -DENABLE_X11_TARGET=OFF -DUSE_GSTREAMER=OFF -DENABLE_TOUCH_EVENTS=OFF ^
  -DUSE_THIN_ARCHIVES=OFF -DENABLE_PCH=OFF
goto :eof

:spike
REM Phase 0: the x86 helper the build must EXECUTE on this host.
cmake --build "%BUILDDIR%" --target LLIntOffsetsExtractor
goto :eof

:build
cmake --build "%BUILDDIR%"
goto :eof

:install
REM Install into %PREFIX% as configured (standalone tree, for inspection).
cmake --install "%BUILDDIR%"
goto :eof

:bundle
REM Produce the combined Qt+WebKit prefix that re_ebook actually consumes.
REM re_ebook uses qmake (QT += webkitwidgets, ebook/ebook.pro), which needs
REM mkspecs\modules\qt_lib_webkitwidgets.pri *inside the Qt prefix* -- so WebKit
REM has to be installed over a copy of the Qt tree, not into a standalone one.
REM Deliberately a copy: a botched install must not damage the only known-good
REM static Qt on the machine.
if "%QTSRC%"==""    set "QTSRC=C:\qt5_static"
if "%BUNDLEDIR%"=="" set "BUNDLEDIR=C:\qt5_static_webkit"
if not exist "%QTSRC%\bin\qmake.exe" (
    echo [bundle] QTSRC is not a Qt prefix: %QTSRC%
    exit /b 1
)
REM BUNDLEDIR is recursively deleted below. Reject roots and any overlap with an
REM input/build prefix before doing that, including an ancestor of one of them.
powershell -NoProfile -Command ^
  "$full = [IO.Path]::GetFullPath($env:BUNDLEDIR);" ^
  "if ($full -eq [IO.Path]::GetPathRoot($full)) { exit 1 };" ^
  "$bundle = $full.TrimEnd('\');" ^
  "foreach ($candidate in @($env:QTSRC, $env:QTDIR, $env:SRC, $env:BUILDDIR, $env:PREFIX, $env:VCPKGINST)) {" ^
  "  if ([string]::IsNullOrWhiteSpace($candidate)) { continue };" ^
  "  $path = [IO.Path]::GetFullPath($candidate).TrimEnd('\');" ^
  "  if ($bundle -ieq $path -or $bundle.StartsWith($path + '\', [StringComparison]::OrdinalIgnoreCase) -or $path.StartsWith($bundle + '\', [StringComparison]::OrdinalIgnoreCase)) { exit 1 }" ^
  "}"
if errorlevel 1 (
    echo [bundle] refusing unsafe BUNDLEDIR: %BUNDLEDIR%
    exit /b 1
)
if exist "%BUNDLEDIR%" (
    REM Accept bundles made before the marker was introduced only when both
    REM characteristic installed files are present.
    if not exist "%BUNDLEDIR%\.qtwebkit-bundle" (
        if not exist "%BUNDLEDIR%\lib\Qt5WebKit.lib" (
            echo [bundle] refusing unrecognized existing directory: %BUNDLEDIR%
            exit /b 1
        )
        if not exist "%BUNDLEDIR%\mkspecs\modules\qt_lib_webkit.pri" (
            echo [bundle] refusing unrecognized existing directory: %BUNDLEDIR%
            exit /b 1
        )
    )
    echo [bundle] removing previous %BUNDLEDIR% ...
    rmdir /s /q "%BUNDLEDIR%"
    if exist "%BUNDLEDIR%" (echo [bundle] failed to remove previous bundle & exit /b 1)
)
echo [bundle] copying %QTSRC% to %BUNDLEDIR% ...
robocopy "%QTSRC%" "%BUNDLEDIR%" /E /NFL /NDL /NJH /NJS /NP >nul
if errorlevel 8 (echo [bundle] robocopy failed & exit /b 1)
> "%BUNDLEDIR%\.qtwebkit-bundle" echo Generated by build-win32-static.bat. Safe to replace.
REM The generated qmake module files bake in CMAKE_INSTALL_PREFIX at *configure*
REM time (QT.webkit.libs = <prefix>/lib). "cmake --install --prefix" only redirects
REM where files are written, so the .pri would still point at the old prefix and
REM qmake consumers fail with LNK1181: cannot open input file 'WebCore.lib'.
REM Reconfigure with the real prefix, regenerate, then install.
echo [bundle] reconfiguring with CMAKE_INSTALL_PREFIX=%BUNDLEDIR% ...
cmake -S "%SRC%" -B "%BUILDDIR%" -DCMAKE_INSTALL_PREFIX="%BUNDLEDIR%"
if errorlevel 1 exit /b 1
echo [bundle] regenerating module files ...
cmake --build "%BUILDDIR%"
if errorlevel 1 exit /b 1
echo [bundle] installing WebKit into %BUNDLEDIR% ...
cmake --install "%BUILDDIR%"
if errorlevel 1 exit /b 1
for %%L in (WebCore JavaScriptCore Qt5WebKit Qt5WebKitWidgets) do (
    if not exist "%BUNDLEDIR%\lib\%%L.lib" (
        echo [bundle] required installed library is missing: %%L.lib
        exit /b 1
    )
)
if not exist "%BUNDLEDIR%\mkspecs\modules\qt_lib_webkit.pri" (
    echo [bundle] required qmake module is missing: qt_lib_webkit.pri
    exit /b 1
)
REM The generated .pri names the third-party static libraries WebCore was built
REM against (ICU, WOFF2, brotli, ...). Those live in vcpkg's tree, not in the Qt
REM prefix, so a consumer of this bundle could not link them. Copy them in so the
REM bundle is self-contained -- that is what re_ebook's CI downloads and uses.
echo [bundle] copying third-party static libs into %BUNDLEDIR%\lib ...
for %%L in (icuuc icuin icudt woff2dec woff2common brotlidec brotlicommon harfbuzz-icu) do (
    if not exist "%VCPKGINST:/=\%\lib\%%L.lib" (
        echo [bundle] required third-party library is missing: %%L.lib
        exit /b 1
    )
    copy /Y "%VCPKGINST:/=\%\lib\%%L.lib" "%BUNDLEDIR%\lib\" >nul
    if errorlevel 1 exit /b 1
)
REM A Qt built with -qt-zlib and friends installs sqlite3, libxml2 and webp into its own prefix
REM under the names a consumer expects. A Qt built against the system libraries installs none of
REM them and the link fails on the first one missing, so fill only the gaps - never replace what
REM the Qt prefix already provides, which is what the consumer was built against. vcpkg names
REM webp differently, hence the rename.
for %%L in (sqlite3 libxml2) do (
    if not exist "%BUNDLEDIR%\lib\%%L.lib" (
        if not exist "%VCPKGINST:/=\%\lib\%%L.lib" (
            echo [bundle] required third-party library is missing: %%L.lib
            exit /b 1
        )
        copy /Y "%VCPKGINST:/=\%\lib\%%L.lib" "%BUNDLEDIR%\lib\" >nul
        if errorlevel 1 exit /b 1
    )
)
if not exist "%BUNDLEDIR%\lib\webp.lib" (
    if not exist "%VCPKGINST:/=\%\lib\libwebp.lib" (
        echo [bundle] required third-party library is missing: libwebp.lib
        exit /b 1
    )
    copy /Y "%VCPKGINST:/=\%\lib\libwebp.lib" "%BUNDLEDIR%\lib\webp.lib" >nul
    if errorlevel 1 exit /b 1
    if exist "%VCPKGINST:/=\%\lib\libsharpyuv.lib" (
        copy /Y "%VCPKGINST:/=\%\lib\libsharpyuv.lib" "%BUNDLEDIR%\lib\" >nul
    )
)
echo [bundle] done: %BUNDLEDIR%
goto :eof
