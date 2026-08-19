@echo off
rem Allow only devices on the current local subnet to reach the companion.
rem The script asks for Administrator permission when needed.
net session >nul 2>&1
if not "%errorlevel%"=="0" (
    powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
    exit /b
)

netsh advfirewall firewall delete rule name="Stopwatch Companion TCP 8787" >nul 2>&1
netsh advfirewall firewall delete rule name="Stopwatch Companion UDP 8788" >nul 2>&1
netsh advfirewall firewall add rule name="Stopwatch Companion TCP 8787" dir=in action=allow protocol=TCP localport=8787 remoteip=LocalSubnet profile=any
if errorlevel 1 goto firewall_error
netsh advfirewall firewall add rule name="Stopwatch Companion UDP 8788" dir=in action=allow protocol=UDP localport=8788 remoteip=LocalSubnet profile=any
if errorlevel 1 goto firewall_error

echo [OK] TCP 8787 and UDP 8788 are available to devices on the local network only.
goto firewall_done

:firewall_error
echo [ERROR] The firewall rules could not be created.

:firewall_done
pause
