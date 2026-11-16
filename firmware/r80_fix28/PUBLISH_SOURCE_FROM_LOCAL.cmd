@echo off
setlocal EnableExtensions
rem Source-only import from verified historical R80 build stage.
rem No Flash, no COM9, no BSD disk read/write.
set "HERE=%~dp0"
if not exist "%HERE%import_full_source.py" (
 echo STOP: Git checkout does not contain import_full_source.py
 exit /b 2
)
if not exist "F:\build\tmp\s94" (
 echo STOP: Historical source stage F:\build\tmp\s94 does not exist.
 echo NO FLASH. No source copied or committed.
 exit /b 4
)
python -B "%HERE%import_full_source.py" --publish
exit /b %errorlevel%
