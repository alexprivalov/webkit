@echo off
REM Maps the Mac share to the drive letters re_ebook's generated Makefiles were written
REM against, and fails loudly if it cannot.
REM
REM Why this exists: build_x86.bat's Makefile tree bakes in absolute paths, and which letter
REM they name depends on what was free when qmake last ran - some subprojects say Y:, others
REM say Z:. "pushd \\Mac\Home\dev\re_ebook" takes whatever letter happens to be free, which is
REM usually neither, and `prlctl exec` runs as SYSTEM with no drive mappings at all. The
REM symptom is jom reporting a library that plainly exists as missing:
REM
REM   Error: dependent 'Y:\dev\re_ebook\build_x86\keyvault\core\release\keyvault-core.lib'
REM          does not exist.
REM
REM Mapping both letters to the share root is the cheap fix. The real fix is a full
REM `build_x86.bat rebuild` so qmake regenerates the paths.

if "%SHAREROOT%"=="" set "SHAREROOT=\\Mac\Home"

REM /persistent:no so a build does not write drive letters into the user profile; on a shared
REM or CI agent those would outlive the run and change what a later build resolves.
net use Y: "%SHAREROOT%" /persistent:no >nul 2>&1
net use Z: "%SHAREROOT%" /persistent:no >nul 2>&1

if not exist Y:\dev\re_ebook\build_x86.bat (
    echo [mapshare] Y: does not reach %SHAREROOT%\dev\re_ebook\build_x86.bat
    exit /b 1
)

if not exist Z:\dev\re_ebook\build_x86.bat (
    echo [mapshare] Z: does not reach %SHAREROOT%\dev\re_ebook\build_x86.bat
    exit /b 1
)
exit /b 0
