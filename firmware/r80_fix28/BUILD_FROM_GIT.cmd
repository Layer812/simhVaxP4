@echo off
setlocal
rem IMPORTANT: Only compile when ALL source files exist in Git.
rem This never accesses COM9 or writes firmware/disks.
set "SRC=%~dp0"
if not exist "%SRC%build_from_git.py" (
 echo STOP: missing build_from_git.py in the same Git checkout
 exit /b 2
)
if not exist "G:\esp-idf-5.5.4\export.bat" (
 echo STOP: ESP-IDF 5.5.4 not installed at G:\esp-idf-5.5.4
 exit /b 2
)
call "G:\esp-idf-5.5.4\export.bat"
if errorlevel 1 exit /b 2
python -B "%SRC%build_from_git.py"
exit /b %errorlevel%
