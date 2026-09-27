@echo off
rem Thin wrapper -- the real build lives in build.ps1.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1" %*
exit /b %errorlevel%
