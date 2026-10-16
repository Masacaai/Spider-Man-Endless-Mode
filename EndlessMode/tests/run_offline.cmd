@echo off
rem Offline check: resolves every native against Spider-Man.exe on disk (mapped read-only, no game code runs).
rem Usage: tests\run_offline.cmd ["path\to\Spider-Man.exe"]
setlocal
set ROOT=%~dp0..
set EXE=%~1
if "%EXE%"=="" set EXE=C:\Program Files (x86)\Steam\steamapps\common\Marvel's Spider-Man Remastered\Spider-Man.exe
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul || exit /b 1
if not exist "%ROOT%\build\tests" mkdir "%ROOT%\build\tests"
cl /nologo /O2 /MT /EHa /std:c++20 /Fo"%ROOT%\build\tests\\" "%ROOT%\tests\resolve_offline.cpp" "%ROOT%\src\scan.cpp" "%ROOT%\src\natives.cpp" "%ROOT%\src\log.cpp" /Fe"%ROOT%\build\tests\resolve_offline.exe" >nul || exit /b 1
"%ROOT%\build\tests\resolve_offline.exe" "%EXE%" "%ROOT%\build\tests\offline.log"
set RC=%ERRORLEVEL%
type "%ROOT%\build\tests\offline.log"
exit /b %RC%
