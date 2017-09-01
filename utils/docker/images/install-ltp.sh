#!/bin/bash -xe

LTP_SRC=$HOME/ltp
TOP_BUILDDIR=$HOME/buildltp
LTP_INSTALL_DIR=$HOME/ltp_install

cd $HOME
mkdir $TOP_BUILDDIR
mkdir $LTP_INSTALL_DIR

git clone https://github.com/linux-test-project/ltp.git
cd ltp
git checkout 20170516 #release 16 may 2017 
make autotools
test -d $TOP_BUILDDIR

cd $TOP_BUILDDIR
$LTP_SRC/configure --prefix=$LTP_INSTALL_DIR

make \
    -C $TOP_BUILDDIR \
    -f $LTP_SRC/Makefile \
    top_srcdir=$LTP_SRC \
    top_builddir=$TOP_BUILDDIR -j4

make \
    -C $TOP_BUILDDIR \
    -f $LTP_SRC/Makefile \
    top_srcdir=$LTP_SRC \
    top_builddir=$TOP_BUILDDIR \
    SKIP_IDCHECK=0 install -j4
