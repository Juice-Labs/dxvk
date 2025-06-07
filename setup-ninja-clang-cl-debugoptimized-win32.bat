set CC=clang-cl
set CXX=clang-cl
meson setup --cross-file win32.txt --backend=ninja --buildtype=debugoptimized build\debugoptimized-win32 