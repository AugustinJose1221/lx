@echo off
rem Build and install lx on Windows.
rem
rem   scripts\install.bat
rem
rem Requires a MinGW toolchain in PATH (e.g. MSYS2: pacman -S
rem mingw-w64-ucrt-x86_64-gcc make). Installs to
rem %LOCALAPPDATA%\Programs\lx and optionally adds it to the user PATH.
rem There are no man pages on Windows; use: lx --help
setlocal
cd /d "%~dp0\.."

set "MAKE="
where make >nul 2>nul && set "MAKE=make"
if not defined MAKE where mingw32-make >nul 2>nul && set "MAKE=mingw32-make"
if not defined MAKE (
    echo error: neither 'make' nor 'mingw32-make' found in PATH.
    echo install MSYS2 from https://www.msys2.org and run:
    echo     pacman -S mingw-w64-ucrt-x86_64-gcc make
    exit /b 1
)

echo ==^> building
%MAKE%
if errorlevel 1 exit /b 1

if not exist "build\lx.exe" (
    echo error: build\lx.exe was not produced. Build from a MinGW/MSYS2
    echo shell so the Makefile selects the Windows backend.
    exit /b 1
)

set "DEST=%LOCALAPPDATA%\Programs\lx"
echo ==^> installing to %DEST%
if not exist "%DEST%" mkdir "%DEST%"
copy /y "build\lx.exe" "%DEST%\lx.exe" >nul
if errorlevel 1 exit /b 1

echo(
echo lx installed: %DEST%\lx.exe
choice /c YN /m "Add %DEST% to your user PATH"
if errorlevel 2 goto :done

rem PowerShell handles PATH safely (setx truncates values over 1024 chars)
powershell -NoProfile -Command ^
  "$d = '%DEST%';" ^
  "$p = [Environment]::GetEnvironmentVariable('Path','User');" ^
  "if (($p -split ';') -notcontains $d) {" ^
  "  [Environment]::SetEnvironmentVariable('Path', ($p.TrimEnd(';') + ';' + $d), 'User');" ^
  "  Write-Host 'PATH updated; open a new terminal to use lx.'" ^
  "} else { Write-Host 'Already in PATH.' }"

:done
echo help: lx --help
endlocal
