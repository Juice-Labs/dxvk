set CC=clang-cl
set CXX=clang-cl
meson setup --prefix=%cd%\build\debug-win32\install --cross-file win32.txt --backend=ninja --buildtype=debug build\debug-win32 