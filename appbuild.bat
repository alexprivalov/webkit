@echo off
REM Stage 3 alone: rebuild re_ebook against whatever is currently installed in the bundle.
REM Use when only app sources changed, or to retry after a stage-3 failure, instead of paying
REM for the engine build again. Log: reebook_build.log.

call "%~dp0mapshare.bat" || exit /b 1

set "QT_DIR=C:\qt5_static_webkit\bin"

REM The post-link copy into Out\ fails silently if a previous binary is still running, which
REM leaves you testing the old one. Delete first so a stale copy cannot masquerade as new.
del /q Y:\dev\re_ebook\build_x86\Out\ebook.exe 2>nul
del /q Y:\dev\re_ebook\build_x86\ebook\release\ebook.exe 2>nul

pushd Y:\dev\re_ebook
call build_x86.bat > "%~dp0reebook_build.log" 2>&1
set "RC=%errorlevel%"
popd

REM build_x86.bat can return before jom's LTCG link has finished, so the exit code alone does
REM not mean the binary is ready. Wait for the toolchain to go quiet.
:waitlink
tasklist /fi "imagename eq link.exe" | find /i "link.exe" >nul && goto :sleeplink
tasklist /fi "imagename eq jom.exe" | find /i "jom.exe" >nul && goto :sleeplink
tasklist /fi "imagename eq cl.exe" | find /i "cl.exe" >nul && goto :sleeplink
goto :done
:sleeplink
timeout /t 10 /nobreak >nul
goto :waitlink

:done
echo APPBUILD_EXIT=%RC%
if not "%RC%"=="0" exit /b %RC%
exit /b 0
