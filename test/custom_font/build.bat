@echo off
setlocal

set ROOT=%~dp0..\..
set OUT=%ROOT%\build\tests\custom_font
set LIB=%ROOT%\build\lib

if not exist "%LIB%\simple_ui.dll" (
  echo Library not found. Run build_lib.bat first.
  exit /b 1
)

if not exist "%OUT%" mkdir "%OUT%"
if not exist "%OUT%\fonts" mkdir "%OUT%\fonts"

echo Building custom_font...
g++ -o "%OUT%\custom_font.exe" ^
  "%~dp0main.cpp" ^
  -I "%LIB%" ^
  -L "%LIB%" -lsimple_ui ^
  -ld3d11 -ldxgi -lgdi32 -luser32 ^
  -mwindows -municode

if errorlevel 1 (
  echo Build failed.
  exit /b 1
)

copy /Y "%LIB%\simple_ui.dll" "%OUT%\simple_ui.dll" >nul
copy /Y "%~dp0icon.ico" "%OUT%\icon.ico" >nul
copy /Y "%~dp0fonts\*" "%OUT%\fonts\" >nul

echo Built %OUT%\custom_font.exe
endlocal
