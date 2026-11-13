@echo off
call "%~dp0build_vs.bat" %*
exit /b %errorlevel%
