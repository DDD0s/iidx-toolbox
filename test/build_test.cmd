@echo off
rem Builds test\smoke.exe and test\random_logic_test.exe.
setlocal
set ROOT=%~dp0..\
set VSWHERE="%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%i in (`%VSWHERE% -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VS=%%i
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
if not exist "%ROOT%build\test" mkdir "%ROOT%build\test"
cl /nologo /O2 /MT /W4 /WX /utf-8 /std:c++latest /EHsc /Fo"%ROOT%build\test\\" "%ROOT%test\smoke.cpp" /Fe"%ROOT%build\test\smoke.exe" || exit /b 1
cl /nologo /O2 /MT /W4 /WX /utf-8 /std:c++latest /EHsc /I "%ROOT%src\core" /Fo"%ROOT%build\test\\" "%ROOT%test\random_logic_test.cpp" /Fe"%ROOT%build\test\random_logic_test.exe" || exit /b 1
cl /nologo /O2 /MT /W4 /WX /utf-8 /std:c++latest /EHsc /I "%ROOT%src\core" /Fo"%ROOT%build\test\\" "%ROOT%test\timing_logic_test.cpp" /Fe"%ROOT%build\test\timing_logic_test.exe" || exit /b 1
cl /nologo /O2 /MT /W4 /WX /utf-8 /std:c++latest /EHsc /Fo"%ROOT%build\test\\" "%ROOT%test\render.cpp" /Fe"%ROOT%build\test\render.exe" d3d9.lib user32.lib || exit /b 1
echo [ok] built test\smoke.exe, random_logic_test.exe and render.exe
endlocal
