@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0.."
cmake -S . -B build-Debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DKERNELCACHE_CUDA=OFF -DKERNELCACHE_BUILD_TESTS=ON -DKERNELCACHE_BUILD_EXAMPLES=ON -DKERNELCACHE_BUILD_BENCHMARKS=ON -DKERNELCACHE_BUILD_CLI=ON -DKERNELCACHE_BUILD_DISTRIBUTED=ON || exit /b 1
cmake --build build-Debug || exit /b 1
echo BUILD_OK
