set CC=clang-cl
set CXX=clang-cl
meson setup --cross-file win32.txt --backend=ninja --buildtype=debug build\debug-win32
