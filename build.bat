@echo off
rem Configure (if needed) and build the x64 Release preset.
rem Requires VCPKG_ROOT to be set.
set CMAKE="C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
%CMAKE% --preset x64-release || exit /b 1
%CMAKE% --build --preset x64-release || exit /b 1
