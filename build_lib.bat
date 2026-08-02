@echo off
setlocal EnableExtensions

cd /d "%~dp0"

if not exist build\lib mkdir build\lib
if not exist build\obj mkdir build\obj
if not exist build\pch mkdir build\pch

set "MAKE=mingw32-make"
where mingw32-make >nul 2>&1
if errorlevel 1 (
  where make >nul 2>&1
  if errorlevel 1 (
    echo mingw32-make not found. Install MSYS2 mingw-w64 make.
    exit /b 1
  )
  set "MAKE=make"
)

set "JOBS=%NUMBER_OF_PROCESSORS%"
if "%JOBS%"=="" set "JOBS=4"
if "%JOBS%"=="0" set "JOBS=4"

echo Building library Release (incremental, -j%JOBS%)...

powershell -NoProfile -Command ^
  "$sw = [System.Diagnostics.Stopwatch]::StartNew();" ^
  "& '%MAKE%' -f Makefile.lib -j%JOBS%;" ^
  "$code = $LASTEXITCODE;" ^
  "$sw.Stop();" ^
  "Write-Host ('Build time: {0:N2}s' -f $sw.Elapsed.TotalSeconds);" ^
  "exit $code"
if errorlevel 1 (
  echo Build failed.
  exit /b 1
)

echo Built build\lib\simple_ui.dll
echo Built build\lib\simple_ui.lib
echo Built build\lib\simple_ui.h
echo Built build\lib\simple_ui\*.h
endlocal
