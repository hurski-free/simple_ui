@echo off
setlocal

set ROOT=%~dp0..\..
set OUT=%ROOT%\build\tests\simple_window
set LIB=%ROOT%\build\lib
set RES=%OUT%\app_res.o

if not exist "%LIB%\simple_ui.dll" (
  echo Library not found. Run build_lib.bat first.
  exit /b 1
)

if not exist "%OUT%" mkdir "%OUT%"

echo Building resources...
pushd "%~dp0"
windres app.rc -O coff -o "%RES%"
set WINDRES_ERR=%ERRORLEVEL%
popd
if %WINDRES_ERR% NEQ 0 (
  echo Resource build failed.
  exit /b 1
)

echo Building simple_window...
g++ -o "%OUT%\simple_window.exe" ^
  "%~dp0main.cpp" ^
  "%RES%" ^
  -I "%LIB%" ^
  -L "%LIB%" -lsimple_ui ^
  -ld3d11 -ldxgi -ld3dcompiler -lgdi32 -luser32 ^
  -mwindows -municode

if errorlevel 1 (
  echo Build failed.
  exit /b 1
)

copy /Y "%LIB%\simple_ui.dll" "%OUT%\simple_ui.dll" >nul
copy /Y "%~dp0icon.ico" "%OUT%\icon.ico" >nul

echo Built %OUT%\simple_window.exe
endlocal
