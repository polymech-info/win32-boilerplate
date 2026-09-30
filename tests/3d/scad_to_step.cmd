@echo off
setlocal

rem Wrapper for scad_to_step.py. Pass all arguments through unchanged.
set "SCRIPT_DIR=%~dp0"
set "SCRIPT=%SCRIPT_DIR%scad_to_step.py"

if not exist "%SCRIPT%" (
  echo ERROR: Cannot find "%SCRIPT%" 1>&2
  exit /b 1
)

where py >nul 2>nul
if %ERRORLEVEL%==0 (
  py -3 "%SCRIPT%" %*
  exit /b %ERRORLEVEL%
)

where python >nul 2>nul
if %ERRORLEVEL%==0 (
  python "%SCRIPT%" %*
  exit /b %ERRORLEVEL%
)

echo ERROR: Python was not found. Install Python 3 or add it to PATH. 1>&2
exit /b 1
