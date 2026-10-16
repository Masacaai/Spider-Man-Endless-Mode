@echo off
rem Builds and runs the wave planner offline: prints the difficulty curve without starting the game.
rem Usage: tests\run_plan.cmd [waves=32] [campaign|riot|bossrush|faction:<id>] [seed=1] [shuffle 0|1]
setlocal
set ROOT=%~dp0..
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul || exit /b 1
if not exist "%ROOT%\build\tests" mkdir "%ROOT%\build\tests"
cl /nologo /O2 /MT /EHa /std:c++20 /Fo"%ROOT%\build\tests\\" "%ROOT%\tests\plan_offline.cpp" "%ROOT%\src\plan.cpp" "%ROOT%\src\roster.cpp" /Fe"%ROOT%\build\tests\plan_offline.exe" >nul || exit /b 1
"%ROOT%\build\tests\plan_offline.exe" %*
