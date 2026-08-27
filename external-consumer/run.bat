@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0.."
set PREFIX=%TEMP%\kc-install-prefix
cmake --install build-Release --prefix "%PREFIX%" || exit /b 1
cd /d "%~dp0"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="%PREFIX%" || exit /b 1
cmake --build build || exit /b 1
build\consumer.exe
