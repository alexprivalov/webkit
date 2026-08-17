@echo off
REM Build one re_ebook subproject: buildsub.bat <build_x86-subdir> [<exe to clear from Out>]
REM
REM   buildsub.bat tests tests.exe
REM   buildsub.bat ebook ebook.exe
REM
REM jom stops the whole tree at the first subproject error, so one binary that cannot relink -
REM typically ebpack_gui.exe while the editor is open, or ebook.exe while the reader is - leaves
REM the others stale. Stale in the worst way: older than the engine they are meant to exercise,
REM while still running and reporting green.
REM
REM Prefer appbuild.bat when nothing is locked; this is the escape hatch.

if "%~1"=="" (echo usage: buildsub.bat ^<subdir^> [exe] & exit /b 2)

call "%~dp0mapshare.bat" || exit /b 1

call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat" >nul
set "QT_DIR=C:\qt5_static_webkit\bin"

REM Clearing the previous binary first: the post-link copy into Out\ fails silently when one is
REM still running, which otherwise leaves you testing the old build.
if not "%~2"=="" (
    del /f /q "Y:\dev\re_ebook\build_x86\Out\%~2" 2>nul
    if exist "Y:\dev\re_ebook\build_x86\Out\%~2" (
        echo [buildsub] ERROR: Could not remove stale Out\%~2 - is it running?
        exit /b 1
    )
)

if not exist "Y:\dev\re_ebook\build_x86\%~1\Makefile.Release" (
    echo [buildsub] no Makefile.Release in build_x86\%~1
    exit /b 1
)

pushd "Y:\dev\re_ebook\build_x86\%~1"
C:\ProgramData\chocolatey\lib\jom\tools\jom.exe -f Makefile.Release > "%~dp0buildsub.log" 2>&1
set "RC=%errorlevel%"
popd

REM jom can return before its LTCG link has finished, so the exit code alone does not mean the
REM binary is ready. Wait for the toolchain to go quiet.
REM The check is machine-wide: it cannot tell our toolchain processes from another build's on
REM the same host, so it is bounded rather than allowed to wait forever. 60 x 10s covers an LTCG
REM link of a ~1GB WebCore.lib with room to spare.
set /a WAITED=0
:waitlink
if %WAITED% GEQ 60 (
    echo [%~n0] WARNING: toolchain still busy after 10 minutes; not waiting further.
    echo [%~n0] The binary may be incomplete, or another build is running on this machine.
    goto :done
)
tasklist /fi "imagename eq link.exe" | find /i "link.exe" >nul && goto :sleeplink
tasklist /fi "imagename eq jom.exe" | find /i "jom.exe" >nul && goto :sleeplink
tasklist /fi "imagename eq cl.exe" | find /i "cl.exe" >nul && goto :sleeplink
goto :done
:sleeplink
set /a WAITED+=1
timeout /t 10 /nobreak >nul
goto :waitlink

:done
echo BUILDSUB_EXIT=%RC%
exit /b %RC%
