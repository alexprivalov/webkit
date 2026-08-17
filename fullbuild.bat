@echo off
REM Engine -> bundle -> re_ebook, the three stages that have to happen in that order.
REM Prints FULLBUILD_DONE only if all three passed. Logs: build.log, bundle.log,
REM reebook_build.log.
REM
REM Run after any engine source change. After changing an ENABLE_/USE_ flag use
REM rebuild_all.bat instead: flags invalidate the whole CMake cache.

call "%~dp0mapshare.bat" || exit /b 1

cd /d "%~dp0"
call build-win32-static.bat build > "%~dp0build.log" 2>&1
if errorlevel 1 (echo WEBKIT_BUILD_FAILED & exit /b 1)
call build-win32-static.bat bundle > "%~dp0bundle.log" 2>&1
if errorlevel 1 (echo BUNDLE_FAILED & exit /b 1)

call "%~dp0appbuild.bat"
echo FULLBUILD_DONE
