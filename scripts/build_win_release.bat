@echo off

REM Environment variables are local only and thus forgotten when script exits
setlocal enabledelayedexpansion

cd /D "%~dp0"
cd ..

REM Test various MSVC settings if available
set VSVARS="C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsx86_amd64.bat"
@if exist %VSVARS% call %VSVARS%

REM Generate build version information
CALL "scripts\\generate_git_build_info.bat"

REM Build solution
msbuild.exe "projects/vs2022/wallet.sln" /p:configuration=Release /nologo /verbosity:minimal /m
