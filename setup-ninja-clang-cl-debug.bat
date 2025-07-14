set CC=clang-cl
set CXX=clang-cl
meson setup --prefix=%cd%\build\debug\install --backend=ninja --buildtype=debug build\debug 