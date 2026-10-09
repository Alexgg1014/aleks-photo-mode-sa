@echo off
rem Builds Aleks.PhotoMode.SA.asi with llvm-mingw (i686). Put its bin folder on PATH first, e.g.
rem   set PATH=C:\llvm-mingw-ucrt-i686\bin;%PATH%
setlocal
cd /d "%~dp0"
i686-w64-mingw32-clang++ -std=c++17 -O2 -Wall -Wextra -Werror -shared -static src\phase1.cpp -o Aleks.PhotoMode.SA.asi ^
  -ld3d9 -lole32 -luuid -lwindowscodecs -lwinmm -lgdi32 || exit /b 1
i686-w64-mingw32-clang++ -std=c++17 -O2 -static src\test_core.cpp -o test_core.exe || exit /b 1
"%~dp0test_core.exe" || exit /b 1
echo Built Aleks.PhotoMode.SA.asi
