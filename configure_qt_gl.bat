@echo off
REM Configure static Qt 5.14.2 WITH OpenGL, into a separate prefix.
REM
REM Differences from the qt5_static build that ships today:
REM   -opengl desktop      instead of -no-opengl -no-opengles3 -no-angle
REM   qtmultimedia kept    (the readme's OpenGL example skipped it; HTML5 media needs it)
REM   deps from vcpkg      the same tree WebKit links, so one build of each library ends up
REM                        in the binary rather than two that drifted apart
REM
REM Desktop GL rather than ANGLE on purpose: WebKit brings its own ANGLE for WebGL, and two
REM static EGL/GLESv2 implementations in one binary collide on symbols.
REM
REM Run against a FRESHLY UNPACKED source tree. Reconfiguring an already-configured tree has
REM produced unreliable results here.

set "SRC=C:\qt5_gl_src\qt-everywhere-src-5.14.2"
set "PREFIX=C:\qt5_static_gl"
set "DEPS=C:\webkit\vcpkg_installed\x86-windows-static"

if not exist "%SRC%\configure.bat" (echo [qtgl] no fresh source at %SRC% & exit /b 1)
if not exist "%DEPS%\lib\libssl.lib" (echo [qtgl] openssl missing from %DEPS% & exit /b 1)

call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat" >nul

cd /d "%SRC%" || exit /b 1

REM vcpkg names differ from what Qt assumes: zlib installs as zs.lib, png as libpng16.lib.
configure -prefix %PREFIX% -I %DEPS%\include -L %DEPS%\lib ^
 -platform win32-msvc -release -opensource -confirm-license -feature-relocatable -strip ^
 -no-shared -static -static-runtime -ltcg ^
 -make libs -make tools -nomake examples -no-compile-examples ^
 -no-dbus -no-icu -system-zlib -system-libjpeg -system-libpng -gif -openssl-linked ^
 -no-gtk -opengl desktop -no-angle ^
 -no-sql-sqlite -no-sql-odbc -no-sqlite ^
 -skip qt3d -skip qtactiveqt -skip qtandroidextras -skip qtcanvas3d -skip qtcharts ^
 -skip qtconnectivity -skip qtdatavis3d -skip qtdeclarative -skip qtdoc -skip qtgamepad ^
 -skip qtgraphicaleffects -skip qtlocation -skip qtmacextras -skip qtpurchasing ^
 -skip qtquickcontrols -skip qtquickcontrols2 -skip qtremoteobjects -skip qtscript ^
 -skip qtscxml -skip qtsensors -skip qtserialbus -skip qtserialport -skip qtspeech ^
 -skip qtvirtualkeyboard -skip qtwayland -skip qtwebengine -skip qtwebglplugin ^
 -skip qtwebview -skip qtx11extras -skip qtxmlpatterns ^
 ZLIB_LIBS="-lzs" LIBPNG_LIBS="-llibpng16" LIBJPEG_LIBS="-ljpeg" ^
 OPENSSL_LIBS="-llibssl -llibcrypto -lzs -lgdi32 -luser32 -lws2_32 -lAdvapi32 -lCrypt32 -lUser32"

echo CONFIGURE_QT_GL_EXIT=%errorlevel%
