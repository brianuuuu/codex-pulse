@echo off
setlocal
cd /d "%~dp0"
where g++ >nul 2>nul
if errorlevel 1 (
 echo Install MinGW-w64 GCC and put its bin directory on PATH. See README.md.
 exit /b 1
)
if not exist dist mkdir dist
pushd assets
windres --preprocessor=g++ --preprocessor-arg=-E --preprocessor-arg=-xc --preprocessor-arg=-DRC_INVOKED -O coff app.rc ..\dist\resources.o
if errorlevel 1 (popd & exit /b 1)
popd
g++ -std=c++17 -Os -s -static -static-libgcc -static-libstdc++ -DUNICODE -D_UNICODE -DWINVER=0x0A00 -D_WIN32_WINNT=0x0A00 -Wall -Wextra -Wpedantic -municode -mwindows -Wl,--no-insert-timestamp -Wl,--dynamicbase -Wl,--nxcompat -Wl,--high-entropy-va src\main.cpp dist\resources.o -o dist\CodexPulse.exe -lgdiplus -lwinhttp -lcomctl32 -lshell32 -lole32 -luuid -ladvapi32 -lbcrypt -ldwmapi -lgdi32 -luser32
if errorlevel 1 exit /b 1
echo Built: dist\CodexPulse.exe
