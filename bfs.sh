#!/bin/bash

echo Build, deploy, run with implementation dependencies
echo

rm `find . -name '*.o'` 2> /dev/null # clean

CODE=$HOME/Code
#ASAN="-fsanitize=address"
ASAN=
CXXX="-O3 -fPIE -fPIC -Wno-deprecated -std=c++11 -march=armv8-a+crc -I.$ASAN"
#CXXX="-O0 -g -fPIE -fPIC -Wno-deprecated -std=c++11 -march=armv8-a+crc -I.$ASAN"
LINK="-Wl,-pie -fuse-ld=gold$ASAN"

for impl in {conf,impl}/*.cpp
do
    if ! $CODE/NDK_CPP/bin/clang++ -c $impl -o $impl.o $CXXX
    then exit 1; fi
done

#WRAPPER=/data/local/tmp/gdbserver :31415
#WRAPPER=time

IMAGE=$1
shift

if $CODE/NDK_CPP/bin/clang++ $IMAGE.cpp {conf,impl}/*.o -o $IMAGE $CXXX $LINK
then
    $CODE/platform-tools/adb push $IMAGE /data/local/tmp/
    # TODO copy the asan lib
    $CODE/platform-tools/adb forward tcp:31415 tcp:31415
    #$CODE/platform-tools/adb shell /data/local/tmp/gdbserver :31415 /data/local/tmp/`basename $IMAGE` $*
    # 'LD_LIBRARY_PATH=/data/local/tmp:$LD_LIBRARY_PATH'
    $CODE/platform-tools/adb shell time /data/local/tmp/`basename $IMAGE` $*
fi
