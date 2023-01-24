@ECHO OFF

REM Environment variables are local only and thus forgotten when script exits
SETLOCAL enabledelayedexpansion

CD /D "%~dp0"
CD ..

REM Test various MSVC settings if available
SET VSVARS="C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsx86_amd64.bat"
@if exist %VSVARS% call %VSVARS%

REM Generate build version information
CALL "scripts\\generate_git_build_info.bat"

REM Build solution
msbuild.exe "projects/vs2022/wallet.sln" /p:BUILD_CONSOLE=TRUE /p:BUILD_MACHINE=TRUE /p:configuration=Release /nologo /verbosity:minimal /m
