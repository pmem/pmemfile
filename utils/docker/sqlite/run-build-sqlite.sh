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


if [[ -z "$WORKDIR"  ]]; then
        echo "ERROR: The variable WORKDIR has to contain a path to " \
        "the root of pmemfile repository."
	exit 1
fi

PMEMFILE_INSTALL_DIR=$HOME/pmemfile_libs
PF_SQL_UTILS_DIR=$WORKDIR/utils/docker/sqlite
SQLITE_DIR=$HOME/sqlite
TEST_DIR=$HOME/testdir
PF_DIR=$TEST_DIR/pf
PF_POOL=$PF_DIR/pmemfile_pool
MOUNTPOINT=$TEST_DIR/mountpoint
MKFS_PMEMFILE=$PMEMFILE_INSTALL_DIR/bin/mkfs.pmemfile

mkdir $TEST_DIR
mkdir $PF_DIR
mkdir $MOUNTPOINT

# Install pmemfile
cd $WORKDIR
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$PMEMFILE_INSTALL_DIR
make install -j2
cmake .. -DCMAKE_BUILD_TYPE=Debug -DCMAKE_INSTALL_PREFIX=$PMEMFILE_INSTALL_DIR
make install -j2

export LD_LIBRARY_PATH=${PMEMFILE_INSTALL_DIR}/lib/pmemfile_debug:${LD_LIBRARY_PATH} 

# Create pmemfile fs
$MKFS_PMEMFILE $PF_POOL 3G

# Run pmemfile testsuite short tests
cd $PF_SQL_UTILS_DIR

set +e

./run-sqlite-testsuite.py -s $SQLITE_DIR -m $MOUNTPOINT \
	-i $PMEMFILE_INSTALL_DIR -p $PF_POOL -t $PF_SQL_UTILS_DIR/short_tests \
	-f $PF_SQL_UTILS_DIR/failing_short_tests \
	--timeout 120

cd $WORKDIR
rm -rf build
