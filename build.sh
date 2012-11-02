#!/bin/sh
BUILD_TYPE=Release
INSTALL_PREFIX=/usr/local
ARGS_EXTRA=
SCAN_BUILD=

if [ ! -d build ]; then
	mkdir build
fi
cd build

if [ "$1" = "debug" ]; then
	BUILD_TYPE=Debug
fi

if [ "$1" = "scan" ]; then
	BUILD_TYPE=Debug
	ARGS_EXTRA+=-DCMAKE_C_COMPILER:string=/usr/bin/ccc-analyzer
	SCAN_BUILD=scan-build
fi

if [ ! -z "$2" ]; then
	INSTALL_PREFIX=$2
fi

if [ ! -z "$CC" ]; then
	ARGS_EXTRA+=-DCMAKE_C_COMPILER:string=$CC
fi

cmake -DCMAKE_INSTALL_PREFIX:string=$INSTALL_PREFIX \
	-DCMAKE_BUILD_TYPE=$BUILD_TYPE \
	$ARGS_EXTRA .. && $SCAN_BUILD make -j4

if [ "$?" = "0" ]; then
	echo
	echo "Build successful. To install:"
	echo "cd build && sudo make install"
	echo
fi
