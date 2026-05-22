@echo off
setlocal enabledelayedexpansion

echo ==========================================================
echo  CUDA Compiler Script for Windows (Parallel Graph Analytics)
echo ==========================================================

:: Default NVCC path on this host
set "NVCC_PATH=C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.6\bin\nvcc.exe"
if not exist "!NVCC_PATH!" (
    :: Try path from environment variable
    if defined CUDA_PATH (
        set "NVCC_PATH=%CUDA_PATH%\bin\nvcc.exe"
    ) else (
        set "NVCC_PATH=nvcc"
    )
)

echo Using NVCC: "!NVCC_PATH!"

:: 1. Try simple direct compilation first
echo.
echo Attempting direct compilation...
"!NVCC_PATH!" -O3 -std=c++17 -o cuda_analysis.exe cuda_analysis.cu
if %errorlevel% equ 0 (
    echo SUCCESS: Compiled cuda_analysis.exe successfully!
    goto :success
)

echo Direct compilation failed (probably cl.exe C++ compiler not in PATH).
echo Searching for Visual Studio MSVC compiler (cl.exe)...

:: 2. Search for typical MSVC installation paths
set "CL_PATH="
for %%V in (2022 2019) do (
    for %%E in (Community Professional Enterprise) do (
        set "VS_DIR=C:\Program Files\Microsoft Visual Studio\%%V\%%E\VC\Tools\MSVC"
        if exist "!VS_DIR!" (
            for /f "delims=" %%D in ('dir /b /ad "!VS_DIR!"') do (
                set "TEST_PATH=!VS_DIR!\%%D\bin\Hostx64\x64"
                if exist "!TEST_PATH!\cl.exe" (
                    set "CL_PATH=!TEST_PATH!"
                    goto :compile_with_ccbin
                )
            )
        )
    )
)

:compile_with_ccbin
if defined CL_PATH (
    echo Found MSVC at: "!CL_PATH!"
    echo Compiling with -ccbin flag pointing to MSVC...
    "!NVCC_PATH!" -O3 -std=c++17 -ccbin "!CL_PATH!" -o cuda_analysis.exe cuda_analysis.cu
    if !errorlevel! equ 0 (
        echo SUCCESS: Compiled cuda_analysis.exe successfully with MSVC binding!
        goto :success
    )
)

echo.
echo ----------------------------------------------------------
echo  MANUAL STEPS REQUIRED:
echo ----------------------------------------------------------
echo Could not automatically compile using local paths. 
echo To compile on Windows, please do the following:
echo   1. Open "Developer Command Prompt for VS 2022" (or your VS version)
echo   2. Navigate to this directory:
echo      cd %~dp0
echo   3. Run the compiler command:
echo      nvcc -O3 -std=c++17 -o cuda_analysis.exe cuda_analysis.cu
echo.
echo Or run inside WSL (Windows Subsystem for Linux) using the Makefile:
echo   make
echo ----------------------------------------------------------
exit /b 1

:success
echo.
echo To run the benchmark:
echo   cuda_analysis.exe ..\outputs\csr_format_web-google
echo.
endlocal
exit /b 0
