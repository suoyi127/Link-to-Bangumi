@echo off
setlocal

set "SERVER=%~dp0build\dev\backend\Debug\anime_vault_server.exe"
if exist "%SERVER%" goto run
set "SERVER=%~dp0build\test\backend\Debug\anime_vault_server.exe"
if exist "%SERVER%" goto run
set "SERVER=%~dp0build\debug\backend\Debug\anime_vault_server.exe"
if exist "%SERVER%" goto run
set "SERVER=%~dp0build\release\backend\Release\anime_vault_server.exe"
if exist "%SERVER%" goto run

echo Backend executable not found. Build it from the repository root:
echo   cmake --preset dev
echo   cmake --build --preset dev
exit /b 1

:run
pushd "%~dp0" || exit /b 1
echo Starting backend: "%SERVER%"
"%SERVER%"
set "SERVER_EXIT=%ERRORLEVEL%"
popd
exit /b %SERVER_EXIT%
