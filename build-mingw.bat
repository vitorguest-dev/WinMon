@echo off
setlocal
if not exist build mkdir build
gcc -std=c11 -O2 -Wall -Wextra -mwindows -o build\WinMon.exe src\main.c -lpdh -liphlpapi -lws2_32 -luser32 -lpsapi -lgdi32 -lshell32 -lcomctl32 -lole32 -loleaut32 -lwbemuuid -ladvapi32 -lcomdlg32 -lwindowscodecs -ldnsapi -luuid
if errorlevel 1 exit /b 1
echo Executavel criado em build\WinMon.exe
