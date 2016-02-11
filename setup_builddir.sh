#!/bin/sh

set -xe

ovjSrcDir=$HOME/src/OpenVnmrJ
ovjBuildDir=$HOME/vjbuild
ovjBuildDir=/tmp/vjbuild
OVJ_TOOLS=$ovjBuildDir/ovjTools

if [ -x $ovjBuildDir ] ; then
    echo "ovjBuildDir already exists at $ovjBuildDir, setup aborted"
    exit
fi

if [ ! -x $ovjSrcDir/src/vnmr/vnmr.h ] ; then
    echo "ovjSrcDir malformed, cant find $ovjSrcDir/src/vnmr/vnmr.h, setup aborted."
    exit
fi

mkdir -p $OVJ_TOOLS
ln -s `which javac` $OVJ_TOOLS/

cp $ovjSrcDir/src/scripts/buildovj $ovjBuildDir/
cp $ovjSrcDir/src/scripts/makeovj $ovjBuildDir/

sed -ie "s|OVJ_BUILD|$ovjBuildDir|" $ovjBuildDir/buildovj
sed -ie "s|OVJ_SOURCE|$ovjSrcDir|" $ovjBuildDir/buildovj

if [ x"$1" = x"build" ] ; then
    cd $ovjBuildDir
    ./buildovj
fi

