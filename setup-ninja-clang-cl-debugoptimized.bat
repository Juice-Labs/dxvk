set CC=clang-cl
set CXX=clang-cl
meson setup --prefix=%cd%\build\debugoptimized\install --backend=ninja --buildtype=debugoptimized build\debugoptimized 