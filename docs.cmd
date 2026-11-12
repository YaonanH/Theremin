@echo off
setlocal
set "DOCS_NODE_DIR="
for /f "delims=" %%I in ('where node.exe 2^>nul') do if not defined DOCS_NODE_DIR set "DOCS_NODE_DIR=%%~dpI"
if defined DOCS_NODE_DIR goto node_found
set "DOCS_NODE_DIR=%~dp0.docs-tools\node-v24.22.3-win-x64\"
if exist "%DOCS_NODE_DIR%node.exe" goto node_found
echo Node.js was not found. Install Node.js LTS with npm, reopen the terminal, and retry.
exit /b 1

:node_found
if not exist "%DOCS_NODE_DIR%npm.cmd" (
    echo npm.cmd was not found beside Node.js. Install Node.js LTS with npm and retry.
    exit /b 1
)
set "PATH=%DOCS_NODE_DIR%;%PATH%"
set "npm_config_cache=%~dp0.docs-tools\npm-cache"
pushd "%~dp0"
if /i "%~1"=="install" goto install
if /i "%~1"=="build" goto build
if /i "%~1"=="preview" goto preview
if "%~1"=="" goto preview
echo Usage: docs.cmd [install ^| preview ^| build]
set "DOCS_EXIT=1"
goto finish

:install
call "%DOCS_NODE_DIR%npm.cmd" ci --ignore-scripts --no-audit --no-fund
goto result

:build
if not exist "node_modules\.bin\myst.cmd" goto missing_dependencies
call "%DOCS_NODE_DIR%npm.cmd" run docs:build
goto result

:preview
if not exist "node_modules\.bin\myst.cmd" goto missing_dependencies
call "%DOCS_NODE_DIR%npm.cmd" run docs:dev
goto result

:missing_dependencies
echo Documentation dependencies are missing. Run .\docs.cmd install first.
set "DOCS_EXIT=1"
goto finish

:result
set "DOCS_EXIT=%errorlevel%"
:finish
popd
exit /b %DOCS_EXIT%
