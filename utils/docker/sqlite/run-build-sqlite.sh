#!/bin/bash -ex
#
# Copyright 2017, Intel Corporation
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#
#     * Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#
#     * Redistributions in binary form must reproduce the above copyright
#       notice, this list of conditions and the following disclaimer in
#       the documentation and/or other materials provided with the
#       distribution.
#
#     * Neither the name of the copyright holder nor the names of its
#       contributors may be used to endorse or promote products derived
#       from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#
# run-build-sqlite.sh - builds sqlite and runs sqlite short tests set
#

START_DIR=`pwd`

SQLITE_DIR=$HOME/SQLite-e2d38d51
SQLITE_LINK=https://www.sqlite.org/src/tarball/SQLite-e2d38d51.tar.gz?uuid=e2d38d51a9cf1c3dfef742507ec76e3d35853bd09b0d09bf2d404c4b036a184d
TEST_DIR=$HOME/testdir
PF_DIR=$TEST_DIR/pf
PF_POOL=$PF_DIR/pmemfile_pool
MOUNTPOINT=$TEST_DIR/mountpoint
MKFS_PMEMFILE=$WORKDIR/build/src/tools/mkfs.pmemfile
PMEMFILE_INSTALL_DIR=$HOME/pmemfile_libs
TCL_INSTALL_DIR=$HOME/tcl_libs
TCL_BIN_DIR=$HOME/tcl8.6.6/unix
TCL_LINK=https://prdownloads.sourceforge.net/tcl/tcl8.6.6-src.tar.gz

mkdir $TEST_DIR
mkdir $PF_DIR
mkdir $MOUNTPOINT

export LD_LIBRARY_PATH=${PMEMFILE_INSTALL_DIR}/lib:${TCL_INSTALL_DIR}/lib:${LD_LIBRARY_PATH} 
export PATH=${TCL_BIN_DIR}:${PATH}

#install tcl
cd $HOME
wget $TCL_LINK 
tar zxf tcl8.6.6-src.tar.gz
cd tcl8.6.6/unix
./configure --prefix=$TCL_INSTALL_DIR
make -j
make install -j

#install sqlite
cd $HOME
wget $SQLITE_LINK -O sqlite.tar.gz
tar zxf sqlite.tar.gz
cd $SQLITE_DIR
./configure
make -j
make smoketest -j

#install pmemfile
cd $WORKDIR
mkdir build; cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug \
	-DCMAKE_INSTALL_PREFIX=$PMEMFILE_INSTALL_DIR > /dev/null
make install -j > /dev/null

#create pmemfile fs
PMEMOBJ_CONF="prefault.at_create=1" $MKFS_PMEMFILE $PF_POOL 3G

#run pmemfile testsuite short tests
cd $START_DIR
./run-sqlite-testsuite.py -s $SQLITE_DIR -m $MOUNTPOINT \
	-r $WORKDIR -p $PF_POOL -t $START_DIR/short_tests \
	-f $START_DIR/failing_short_tests \
	--timeout 120

