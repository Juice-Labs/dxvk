set CC=clang-cl
set CXX=clang-cl
meson setup --prefix=%cd%\build\debugoptimized-win32\install --cross-file win32.txt --backend=ninja --buildtype=debugoptimized build\debugoptimized-win32 