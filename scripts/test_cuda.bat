@echo off
setlocal
cd /d "%~dp0..\build-CUDA-Release"
ctest --output-on-failure
