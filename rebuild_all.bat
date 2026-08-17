@echo off
REM configure + fullbuild. Needed after changing any ENABLE_/USE_ flag, because that
REM invalidates the whole CMake cache and a plain `build` would not pick it up.
REM Log: configure.log, then whatever fullbuild.bat writes.
REM
REM Note that WEBKIT_OPTION_DEFINE marks most feature flags PRIVATE, so they do NOT appear in
REM configure's printed feature summary. Confirm them in C:\webkit-build\CMakeCache.txt:
REM   findstr /i "ENABLE_DARK_MODE_CSS: ENABLE_XSLT:" C:\webkit-build\CMakeCache.txt

cd /d "%~dp0"
call build-win32-static.bat configure > "%~dp0configure.log" 2>&1
if errorlevel 1 (echo CONFIGURE_FAILED & exit /b 1)
call "%~dp0fullbuild.bat"
