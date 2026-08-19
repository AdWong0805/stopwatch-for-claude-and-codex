@echo off
rem Installs a hidden autostart entry for the companion, then starts it now.
rem No chcp here: the VBS must be written in the system ANSI codepage so
rem WScript reads non-ASCII paths correctly.
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

set "INSTALL_DIR=%LOCALAPPDATA%\StopwatchCompanion"
set "COMPANION_SCRIPT=%INSTALL_DIR%\claude_codex_companion.py"
if not exist "%INSTALL_DIR%" mkdir "%INSTALL_DIR%"
copy /y "%~dp0claude_codex_companion.py" "%COMPANION_SCRIPT%" >nul
if errorlevel 1 (
    echo [ERROR] Could not install the companion into:
    echo         %INSTALL_DIR%
    pause
    exit /b 1
)
if exist "%~dp0usage_override.json" copy /y "%~dp0usage_override.json" "%INSTALL_DIR%\usage_override.json" >nul

set "VBS=%APPDATA%\Microsoft\Windows\Start Menu\Programs\Startup\stopwatch_companion.vbs"
(
echo Set shell = CreateObject("WScript.Shell"^)
echo shell.Run """%PY_EXE%"" ""%COMPANION_SCRIPT%""", 0, False
) > "%VBS%"

if exist "%VBS%" (
    echo [OK] Companion installed to a stable folder and starts hidden at every login.
    echo      To remove it later, delete this file:
    echo      %VBS%
    echo.
    echo Starting companion in the background now...
    wscript "%VBS%"
    echo Done. Verify: open http://127.0.0.1:8787/usage in your browser.
) else (
    echo [ERROR] Could not create the autostart entry.
)
pause
