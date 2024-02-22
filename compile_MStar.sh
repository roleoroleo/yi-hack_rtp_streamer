#!/bin/bash

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

cd fdk-aac || exit 1
make clean
make || exit 1
make install
mkdir ../live/lib/
cp -f ./_install/lib/libfdk-aac.a ../live/lib/
cp -rf ./_install/include/* ../live/include/
cd ..

cd live || exit 1
make clean
make || exit 1

mkdir -p ../_install/bin || exit 1

cp ./rAudioStreamer ../_install/bin || exit 1
cp ./rAudioReceiver ../_install/bin || exit 1

arm-linux-gnueabihf-strip ../_install/bin/* || exit 1
