Python version 3.7.1 cross-compiled for Windows using Mingw-w64
===============================================================

This is an experiment to cross-compile Python 3.7.1 for Windows from 
Linux using MXE (https://mxe.cc). So far I have gotten to the point
that the Python library and executable compile both with "--enable-shared"
and "--disable-shared". There are still issues with paths to extensions 
(.pyd files).

Install MXE under "/opt/mxe" and build the necessary 
prerequisites for a 64-bit shared target such as "cc" and "libffi". Then,
clone this repository into a local folder "python.orig". 
The following script can be used to build a shared library Python version that
should run under Windows:

```sh
# Use the native gcc compiler to build a bootstrap python executable
# --disable-shared and -O0 reduce compilation time and complexity
mkdir -p python.native_build && cd python.native_build
../python.orig/configure \
    --disable-shared \
    CFLAGS='-O0'
cd ..
make -C python.native_build -j 4 python

# From now on, use the MXE cross-compiler
PATH=/opt/mxe/usr/bin:$PATH
TARGET=x86_64-w64-mingw32.shared
BUILD=x86_64-pc-linux-gnu

mkdir -p python.mingw_build && cd python.mingw_build && \
ac_cv_file__dev_ptmx=no \
ac_cv_file__dev_ptc=no \
../python.orig/configure \
    --with-pydebug \
    --prefix=$PREFIX \
    --host=$TARGET \
    --build=$BUILD \
    --enable-shared \
    --with-system-ffi \
    --without-ensurepip \
    --without-c-locale-coercion \
    CROSSCOMPILING_PYTHON_FOR_BUILD=../python.native_build/python \
    CPPFLAGS='-D_WIN32_WINNT=_WIN32_WINNT_WIN7'
cd ..
make -C python.mingw_build -j 4
```

