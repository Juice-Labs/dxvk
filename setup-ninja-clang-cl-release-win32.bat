set CC=clang-cl
set CXX=clang-cl
meson setup --prefix=%cd%\build\release-win32\install --cross-file win32.txt --backend=ninja --buildtype=release build\release-win32 