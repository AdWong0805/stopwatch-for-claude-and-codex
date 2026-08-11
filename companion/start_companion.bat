@echo off
chcp 65001 >nul
cd /d "%~dp0"
set "PY_EXE="

for /d %%D in ("%LOCALAPPDATA%\Programs\Python\Python3*") do if exist "%%~fD\python.exe" set "PY_EXE=%%~fD\python.exe"

if not defined PY_EXE if exist "%USERPROFILE%\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe" set "PY_EXE=%USERPROFILE%\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe"

if not defined PY_EXE for /f "delims=" %%P in ('where python.exe 2^>nul') do if not defined PY_EXE set "PY_EXE=%%~fP"

if not defined PY_EXE for /f "delims=" %%P in ('py -3 -c "import sys; print(sys.executable)" 2^>nul') do if not defined PY_EXE set "PY_EXE=%%~fP"

if not defined PY_EXE (
    echo [ERROR] Python not found. Install Python 3, then run this file again.
    pause
    exit /b 1
)

echo [INFO] Starting Stopwatch companion with:
echo        %PY_EXE%
"%PY_EXE%" claude_codex_companion.py
pause
