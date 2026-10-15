@echo off
rem Builds build\iidx_toolbox.dll (x64, static CRT) with the newest Visual Studio.
setlocal
set ROOT=%~dp0
set VSWHERE="%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%i in (`%VSWHERE% -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VS=%%i
if not defined VS (
  echo [error] Visual Studio with C++ tools not found
  exit /b 1
)
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
for %%d in (minhook imgui core features) do if not exist "%ROOT%build\obj\%%d" mkdir "%ROOT%build\obj\%%d"
set INC=/I "%ROOT%src" /I "%ROOT%src\core" /I "%ROOT%vendor\minhook\include" /I "%ROOT%vendor\imgui" /I "%ROOT%vendor\imgui\backends"
set CXX=cl /nologo /O2 /MT /utf-8 /std:c++latest /EHsc /DNOMINMAX %INC%
cl /nologo /O2 /MT /W3 /Fo"%ROOT%build\obj\minhook\\" /c "%ROOT%vendor\minhook\src\buffer.c" "%ROOT%vendor\minhook\src\hook.c" "%ROOT%vendor\minhook\src\trampoline.c" "%ROOT%vendor\minhook\src\hde\hde64.c" || exit /b 1
%CXX% /W3 /Fo"%ROOT%build\obj\imgui\\" /c "%ROOT%vendor\imgui\imgui*.cpp" "%ROOT%vendor\imgui\backends\imgui_impl_dx9.cpp" || exit /b 1
%CXX% /W4 /WX /Fo"%ROOT%build\obj\core\\" /c "%ROOT%src\plugin.cpp" "%ROOT%src\core\*.cpp" || exit /b 1
if exist "%ROOT%src\features\*.cpp" (
  %CXX% /W4 /WX /Fo"%ROOT%build\obj\features\\" /c "%ROOT%src\features\*.cpp" || exit /b 1
)
link /nologo /DLL /OUT:"%ROOT%build\iidx_toolbox.dll" "%ROOT%build\obj\minhook\*.obj" "%ROOT%build\obj\imgui\*.obj" "%ROOT%build\obj\core\*.obj" "%ROOT%build\obj\features\*.obj" user32.lib gdi32.lib dwmapi.lib imm32.lib avrt.lib || exit /b 1
echo [ok] built %ROOT%build\iidx_toolbox.dll
endlocal
