@echo off
rem Build client-manager with MSVC (Win11). Prefers CMake; falls back to cl.
setlocal

where cmake >nul 2>&1
if %errorlevel%==0 (
    echo [build] Using CMake + Visual Studio generator...
    cmake -S . -B build -G "Visual Studio 17 2022" -A x64
    if errorlevel 1 goto :fail
    cmake --build build --config Release
    if errorlevel 1 goto :fail
    echo [build] Done: build\Release\client-manager.exe
    goto :end
)

echo [build] CMake not found, falling back to cl...
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 (
    echo [build] vcvars64.bat not found. Install Visual Studio 2022 with
    echo [build] "Desktop development with C++", or install CMake.
    goto :fail
)
cl /std:c++17 /EHsc /W4 /O2 /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX /D_WIN32_WINNT=0x0A00 /Isrc /Ithird_party src\*.cpp /Fe:client-manager.exe advapi32.lib iphlpapi.lib winhttp.lib ws2_32.lib ole32.lib oleaut32.lib wbemuuid.lib shell32.lib secur32.lib taskschd.lib version.lib user32.lib kernel32.lib
if errorlevel 1 goto :fail
echo [build] Done: client-manager.exe
goto :end

:fail
echo [build] FAILED
endlocal
exit /b 1

:end
endlocal
exit /b 0
