@echo off
setlocal EnableExtensions
rem Publish SHA-verified original sources to the R80 Git development branch,
rem fetch a clean checkout from Git and compile. Never run flash or esptool.
set "HERE=%~dp0"
rem The first source import is intentionally one-time. If the complete
rem full_project source manifest is already COMMITTED in this branch, do not
rem attempt to import again: clone it from GitHub and build directly.
rem git cat-file checks the committed tree, not an untracked local folder.
git -C "%HERE%..\.." cat-file -e "HEAD:firmware/r80_fix28/full_project/SOURCE_IMPORT_MANIFEST.json" >nul 2>&1
if errorlevel 1 (
 echo R80 complete source is not committed yet. Starting one-time SHA-verified import.
 call "%HERE%PUBLISH_SOURCE_FROM_LOCAL.cmd"
 if errorlevel 1 (
  echo STOP: Source publication did not pass. Native build not attempted.
  exit /b 4
 )
) else (
 echo PASS: R80 complete source is already committed. Skipping one-time import.
 echo The following build uses a fresh clone from GitHub, never this local tree.
)
rem Before ANY fresh native build, audit 1,777 tracked source hashes and
rem recover exactly the one excluded .inl from the SHA-verified historical
rem donor. It is then committed to this Git branch. No version substitution.
python -B "%HERE%source_completion_gate.py" --publish
if errorlevel 1 (
 echo STOP: Git source audit/recovery incomplete. Native build NOT attempted.
 exit /b 4
)
set "BASE=F:\build\tmp"
if not exist "%BASE%" (
 echo STOP: F:\build\tmp not available.
 exit /b 4
)
for /f %%i in ('powershell -NoProfile -Command "Get-Date -Format yyyyMMdd_HHmmss"') do set "STAMP=%%i"
if not defined STAMP (
 echo STOP: Failed to choose unique clean Git build directory.
 exit /b 4
)
set "NEW=%BASE%\simhVaxP4_R80_FROM_GIT_%STAMP%"
if exist "%NEW%" (
 echo STOP: Destination exists. No checkout overwritten.
 exit /b 4
)
git clone --single-branch --branch work/r80-regression-handoff-20261115 https://github.com/Layer812/simhVaxP4.git "%NEW%"
if errorlevel 1 (
 echo STOP: Clean Git checkout failed. No compile or flash.
 exit /b 4
)
call "%NEW%\firmware\r80_fix28\BUILD_FROM_GIT.cmd"
if errorlevel 1 (
 echo BUILD FAILED OR SAFELY BLOCKED. No flash attempted.
 exit /b 4
)
echo PASS: Clean Git checkout native source compilation succeeded.
echo NO FLASH. Source revision was committed to a development branch only.
exit /b 0
