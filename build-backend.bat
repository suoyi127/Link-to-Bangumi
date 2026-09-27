@echo off
setlocal

if not "%~1"=="" set "VCPKG_ROOT=%~1"
if not defined VCPKG_ROOT set /p "VCPKG_ROOT=Enter vcpkg root: "
set "VCPKG_ROOT=%VCPKG_ROOT:"=%"
if not defined VCPKG_ROOT (
    echo VCPKG_ROOT is empty. Point it to your vcpkg installation.
    exit /b 1
)
if not exist "%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" (
    echo VCPKG_ROOT does not contain scripts\buildsystems\vcpkg.cmake.
    exit /b 1
)
where cmake >nul 2>nul
if errorlevel 1 (
    echo CMake was not found in PATH.
    exit /b 1
)

pushd "%~dp0" || exit /b 1
cmake --preset dev
set "BUILD_RESULT=%ERRORLEVEL%"
if not "%BUILD_RESULT%"=="0" goto finish
cmake --build --preset dev
set "BUILD_RESULT=%ERRORLEVEL%"

:finish
popd
exit /b %BUILD_RESULT%
