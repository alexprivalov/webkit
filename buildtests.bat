@echo off
REM Build only the tests subproject. Useful when another subproject cannot relink - typically
REM ebpack_gui, whose Out\ copy fails while the editor is running - because jom stops the whole
REM tree at that error and leaves tests.exe stale, i.e. older than the engine it should exercise.
call "%~dp0mapshare.bat" || exit /b 1

call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat" >nul
set "QT_DIR=C:\qt5_static_webkit\bin"

del /f /q Y:\dev\re_ebook\build_x86\Out\tests.exe 2>nul
if exist Y:\dev\re_ebook\build_x86\Out\tests.exe (
    echo [buildtests] ERROR: Could not remove stale Out\tests.exe
    exit /b 1
)

pushd Y:\dev\re_ebook\build_x86\tests
C:\ProgramData\chocolatey\lib\jom\tools\jom.exe -f Makefile.Release > "%~dp0tests_build.log" 2>&1
set "RC=%errorlevel%"
popd

:waitlink
tasklist /fi "imagename eq link.exe" | find /i "link.exe" >nul && goto :sleeplink
tasklist /fi "imagename eq jom.exe" | find /i "jom.exe" >nul && goto :sleeplink
tasklist /fi "imagename eq cl.exe" | find /i "cl.exe" >nul && goto :sleeplink
goto :done
:sleeplink
timeout /t 10 /nobreak >nul
goto :waitlink

:done
echo BUILDTESTS_EXIT=%RC%
exit /b %RC%
