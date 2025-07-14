set CC=clang-cl
set CXX=clang-cl
meson setup --prefix=%cd%\build\release\install --backend=ninja --buildtype=release build\release 