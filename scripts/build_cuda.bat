@echo off
setlocal
set BUILD_TYPE=%1
if "%BUILD_TYPE%"=="" set BUILD_TYPE=Release
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0.."
echo === Building CUDA %BUILD_TYPE% ===
cmake -S . -B build-CUDA-%BUILD_TYPE% -G Ninja -DCMAKE_BUILD_TYPE=%BUILD_TYPE% -DKERNELCACHE_CUDA=ON -DKERNELCACHE_BUILD_TESTS=ON -DKERNELCACHE_BUILD_EXAMPLES=OFF -DKERNELCACHE_BUILD_BENCHMARKS=OFF -DKERNELCACHE_BUILD_CLI=OFF -DKERNELCACHE_BUILD_DISTRIBUTED=OFF || exit /b 1
cmake --build build-CUDA-%BUILD_TYPE% || exit /b 1
echo BUILD_OK
