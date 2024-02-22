#!/bin/bash

ARCHIVE=live.2023.01.19.tar.gz
FDKAAC_VER=2.0.3
ARCHIVE_FDKAAC=v${FDKAAC_VER}.tar.gz

export PATH=${PATH}:/opt/yi/arm-linux-gnueabihf-4.8.3-201404/bin

export TARGET=arm-linux-gnueabihf
export CROSS=arm-linux-gnueabihf
export BUILD=x86_64-pc-linux-gnu

export CROSSPREFIX=${CROSS}-

export STRIP=${CROSSPREFIX}strip
export CXX=${CROSSPREFIX}g++
export CC=${CROSSPREFIX}gcc
export LD=${CROSSPREFIX}ld
export AS=${CROSSPREFIX}as
export AR=${CROSSPREFIX}ar

SCRIPT_DIR=$(cd `dirname $0` && pwd)
cd $SCRIPT_DIR

rm -rf ./_install
rm -rf ./live
rm -rf ./fdk-aac

# fdk-aac
if [ ! -f ${ARCHIVE_FDKAAC} ]; then
    wget https://github.com/mstorsjo/fdk-aac/archive/${ARCHIVE_FDKAAC}
fi
tar zxvf ${ARCHIVE_FDKAAC}
mv fdk-aac-${FDKAAC_VER} fdk-aac
cd fdk-aac

./autogen.sh || exit 1

./configure CC=arm-linux-gnueabihf-gcc \
    --prefix=$SCRIPT_DIR/fdk-aac/_install \
    CFLAGS="-Os -mcpu=cortex-a7 -mfpu=neon-vfpv4 -I/opt/yi/arm-linux-gnueabihf-4.8.3-201404/arm-linux-gnueabihf/libc/usr/include -L/opt/yi/arm-linux-gnueabihf-4.8.3-201404/arm-linux-gnueabihf/libc/lib/arm-linux-gnueabihf" \
    AR=arm-linux-gnueabihf-ar \
    RANLIB=arm-linux-gnueabihf-ranlib \
    --host=arm \
    || exit 1

cd ..

# live555
if [ ! -f $ARCHIVE ]; then
    wget https://download.videolan.org/pub/contrib/live555/$ARCHIVE
fi
tar zxvf $ARCHIVE
find live -type d -exec chmod 755 {} +
find live -type f -name "*.cpp" -exec chmod 644 {} +
find live -type f -name "*.hh" -exec chmod 644 {} +
find live -type f -name "Makefile*" -exec chmod 644 {} +

patch -p0 < cross.patch

cd live || exit 1

./genMakefiles linux-cross
cp -f ../Makefile.template Makefile
cp -rf ../src .
cp -rf ../include .
