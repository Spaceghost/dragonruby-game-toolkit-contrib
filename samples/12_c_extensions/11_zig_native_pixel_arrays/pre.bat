@echo off
setlocal
cd /d "%~dp0"
if not defined ZIG set "ZIG=zig"
if not defined DRB_ROOT set "DRB_ROOT=..\..\.."
if not defined ZIG_TARGET set "ZIG_TARGET=x86_64-windows-gnu"
if not defined DRB_PLATFORM set "DRB_PLATFORM=windows-amd64"
if exist "%DRB_ROOT%\dragonruby.h" goto build
if exist "%DRB_ROOT%\include\dragonruby.h" goto build
echo Set DRB_ROOT to the matching DragonRuby SDK root. 1>&2
exit /b 1
:build
"%ZIG%" build extension --prefix . -Doptimize=ReleaseSafe -Dcpu=baseline "-Dtarget=%ZIG_TARGET%" "-Dplatform=%DRB_PLATFORM%" "-Ddragonruby-root=%DRB_ROOT%" %*
exit /b %errorlevel%
