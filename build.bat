@echo off
setlocal

set "ROOT=%~dp0"
set "NAME=umb"
set "OBJDIR=%ROOT%obj"
set "BINDIR=%ROOT%bin"

rem --- MSVC toolchain --------------------------------------------------------
where cl.exe >nul 2>&1
if not errorlevel 1 goto :have_cl

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto :no_msvc
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
if not defined VSPATH goto :no_msvc
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 goto :no_msvc

:have_cl
if not exist "%OBJDIR%" mkdir "%OBJDIR%"
if not exist "%BINDIR%" mkdir "%BINDIR%"

rem --- Compile + link (debug) ------------------------------------------------
cl /nologo /W3 /Zi /Od /MDd ^
   /I"%ROOT%include" /I"%ROOT%include\raylib" /I"%ROOT%src" ^
   /Fo"%OBJDIR%\\" /Fd"%OBJDIR%\%NAME%.pdb" ^
   "%ROOT%src\*.c" ^
   /link /DEBUG /STACK:8388608 /OUT:"%BINDIR%\%NAME%.exe" /PDB:"%BINDIR%\%NAME%.pdb" ^
   /LIBPATH:"%ROOT%lib" raylib.lib WinMM.lib
if errorlevel 1 goto :failed

echo.
echo Build OK -^> bin\%NAME%.exe
exit /b 0

:no_msvc
echo ERROR: cl.exe not found and no Visual Studio C++ toolchain could be located.
echo        Run this from a "x64 Native Tools Command Prompt", or install the
echo        "Desktop development with C++" workload.
exit /b 1

:failed
echo.
echo Build FAILED.
exit /b 1
