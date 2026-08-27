@echo off
setlocal
set BUILD_TYPE=%1
if "%BUILD_TYPE%"=="" set BUILD_TYPE=Release
set CUDA=%2
if "%CUDA%"=="" set CUDA=OFF
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0.."
echo === Building %BUILD_TYPE% (CUDA=%CUDA%) ===
cmake -S . -B build-%BUILD_TYPE% -G Ninja -DCMAKE_BUILD_TYPE=%BUILD_TYPE% -DKERNELCACHE_CUDA=%CUDA% -DKERNELCACHE_BUILD_TESTS=ON -DKERNELCACHE_BUILD_EXAMPLES=ON -DKERNELCACHE_BUILD_BENCHMARKS=ON -DKERNELCACHE_BUILD_CLI=ON -DKERNELCACHE_BUILD_DISTRIBUTED=ON || exit /b 1
cmake --build build-%BUILD_TYPE% || exit /b 1
echo BUILD_OK