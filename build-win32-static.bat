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
REM Usage: build-win32-static.bat [deps|configure|spike|build|install]
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
  -DENABLE_VIDEO=OFF -DENABLE_WEB_AUDIO=OFF -DENABLE_XSLT=OFF ^
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
cmake --install "%BUILDDIR%"
goto :eof
