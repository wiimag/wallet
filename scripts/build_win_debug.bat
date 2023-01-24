@echo off

REM Environment variables are local only and thus forgotten when script exits
setlocal enabledelayedexpansion

cd /D "%~dp0"
cd ..

REM Test various MSVC settings if available
set VSVARS="C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsx86_amd64.bat"
@if exist %VSVARS% call %VSVARS%

REM Update build version information
CALL "scripts\\generate_git_build_info.bat"

REM Invoke msbuild to generate solution
msbuild.exe "projects/vs2022/wallet.sln" /p:configuration=Debug /nologo /verbosity:normal /m
