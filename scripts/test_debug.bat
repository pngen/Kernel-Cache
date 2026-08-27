@echo off
setlocal
cd /d "%~dp0..\build-Debug"
ctest --output-on-failure
