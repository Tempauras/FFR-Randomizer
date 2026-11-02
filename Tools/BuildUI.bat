@echo off
rem Builds the UI pak and installs it into the game. Any arguments are passed on to BuildUI.ps1.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0BuildUI.ps1" -Install %*
set "BUILD_RESULT=%ERRORLEVEL%"
echo.
if "%BUILD_RESULT%"=="0" (echo Build finished.) else (echo Build failed. See the message above.)
pause
exit /b %BUILD_RESULT%
