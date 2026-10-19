@echo off
rem Builds the Vibeless dxgi.dll proxy and its test with the Visual Studio x64 toolchain already on PATH
rem (run from a vcvars64 prompt). Usage: build.cmd <output folder>
setlocal
if "%~1"=="" (echo usage: build.cmd ^<output folder^> & exit /b 2)
set OUT=%~1
if not exist "%OUT%\proxy" mkdir "%OUT%\proxy"
if not exist "%OUT%\obj" mkdir "%OUT%\obj"
cl /nologo /std:c++20 /EHsc /W4 /WX /O2 /MT /LD /Brepro "%~dp0dxgi.cpp" /Fo"%OUT%\obj\\" /Fe"%OUT%\proxy\dxgi.dll" /link /DEF:"%~dp0dxgi.def" /NOLOGO /Brepro || exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /WX /O2 /MT /LD "%~dp0tests\stub-runtime.cpp" /Fo"%OUT%\obj\\" /Fe"%OUT%\proxy\UndauntedInternalServer.dll" /link /NOLOGO || exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /WX /O2 /MT "%~dp0tests\proxy-test.cpp" /Fo"%OUT%\obj\\" /Fe"%OUT%\proxy-test.exe" /link /NOLOGO || exit /b 1
"%OUT%\proxy-test.exe" "%OUT%\proxy" || exit /b 1
exit /b 0
