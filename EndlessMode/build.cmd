@echo off
rem Builds EndlessMode.dll and EndlessMode.script into build\
setlocal
set ROOT=%~dp0
set VS=C:\Program Files\Microsoft Visual Studio\2022\Community
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1

if not exist "%ROOT%vcpkg_installed\x64-windows-static\lib\minhook.x64.lib" (
    "%VS%\VC\vcpkg\vcpkg.exe" install --triplet x64-windows-static --x-manifest-root="%ROOT%." --x-install-root="%ROOT%vcpkg_installed" || exit /b 1
)

set INC=%ROOT%vcpkg_installed\x64-windows-static\include
set LIB_DIR=%ROOT%vcpkg_installed\x64-windows-static\lib
if not exist "%ROOT%build" mkdir "%ROOT%build"

cl /nologo /LD /O2 /MT /EHa /std:c++20 /W3 /Zi /DNDEBUG /DNOMINMAX /DWIN32_LEAN_AND_MEAN /I"%INC%" /Fo"%ROOT%build\\" /Fd"%ROOT%build\\" ^
   "%ROOT%src\*.cpp" /Fe"%ROOT%build\EndlessMode.dll" ^
   /link /LIBPATH:"%LIB_DIR%" minhook.x64.lib user32.lib shell32.lib hid.lib setupapi.lib /DEBUG /MAP /OPT:REF /OPT:ICF || exit /b 1

powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%pack.ps1" || exit /b 1
echo BUILD OK
