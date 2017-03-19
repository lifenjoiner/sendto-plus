@echo off
pushd %~dp0

cl.exe /MD /Os /DUNICODE /D_UNICODE sendto+.c Ole32.lib shell32.lib user32.lib Comdlg32.lib Shlwapi.lib

popd
