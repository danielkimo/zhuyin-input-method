@echo off
setlocal

REM Update DLL_PATH if your Visual Studio generator places the built DLL elsewhere.
set "DLL_PATH=%~dp0..\build\windows-ime\Release\zhuyin_ime.dll"

net session >nul 2>&1
if errorlevel 1 (
  echo This script must run as Administrator.
  echo Windows may also show a SmartScreen warning for an unsigned DLL; code signing is recommended for production.
  powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
  exit /b 1
)

if not exist "%DLL_PATH%" (
  echo Built DLL not found: "%DLL_PATH%"
  echo Adjust DLL_PATH in this script to point at your actual Release build output.
  exit /b 1
)

regsvr32 /u "%DLL_PATH%"
if errorlevel 1 (
  echo Unregistration failed.
  exit /b 1
)

echo Zhuyin IME unregistered successfully.
exit /b 0
