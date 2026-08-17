@echo off
REM Compile a single engine translation unit: compileone.bat <ninja target>
REM
REM   compileone.bat Source/WebCore/CMakeFiles/WebCore.dir/platform/qt/RenderThemeQt.cpp.obj
REM
REM For checking a small edit without paying for a full engine build. vcvars must be applied
REM with `call` from a script: chaining it behind `&` on one command line leaves the standard
REM library off the include path, and the failure looks like a missing <type_traits>.
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat" >nul

set "CMDIR=C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake"
set "PATH=%CMDIR%\Ninja;%CMDIR%\CMake\bin;%PATH%"

cd /d C:\webkit-build || exit /b 1
ninja %*
echo COMPILEONE_EXIT=%errorlevel%
