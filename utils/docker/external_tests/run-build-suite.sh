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
	echo "ERROR: The variable WORKDIR has to contain a path to"\
	"the root of pmemfile repository."
	exit 1
fi

if [[ -z "$1" ]]; then
	echo "ERROR: First argument has to contain a name of test suite"\
	"to be run (sqlite|ltp)."
	exit 1
fi

if [[ "$2" == "verbose" ]]; then
	VERBOSE="--verbose"
fi

PMEMFILE_INSTALL_DIR=$HOME/pmemfile_libs
PMEMFILE_LIB_DIR=$PMEMFILE_INSTALL_DIR/lib/pmemfile_debug
TEST_UTILS_DIR=$WORKDIR/utils/docker/external_tests
TEST_DIR=$HOME/testdir
PF_DIR=$TEST_DIR/pf
PF_POOL=$PF_DIR/pmemfile_pool
MOUNTPOINT=$TEST_DIR/mountpoint
MKFS_PMEMFILE=$PMEMFILE_INSTALL_DIR/bin/mkfs.pmemfile
PMEMFILE_SHARED_OPTS=".. -DCMAKE_INSTALL_PREFIX=$PMEMFILE_INSTALL_DIR"

if [ "$COVERAGE" = "1" ]; then
	PMEMFILE_SHARED_OPTS="${PMEMFILE_SHARED_OPTS} -DCMAKE_C_FLAGS=-coverage -DCMAKE_CXX_FLAGS=-coverage"
fi

if [[ "$1" == "sqlite" ]]; then
	SUITE_DIR=$HOME/sqlite
	SUITE_UTILS_DIR=$TEST_UTILS_DIR/sqlite
elif [[ "$1" == "ltp" ]]; then
	SUITE_DIR=$HOME/ltp_install
	SUITE_UTILS_DIR=$TEST_UTILS_DIR/ltp
else
	echo "First argument doesn't match any of existing suites"\
	"(sqlite|ltp)."
	exit 1
fi

TESTS=$SUITE_UTILS_DIR/short_tests
FAILING_TESTS=$SUITE_UTILS_DIR/failing_short_tests

mkdir $TEST_DIR
mkdir $PF_DIR
mkdir $MOUNTPOINT

# Install pmemfile
cd $WORKDIR
mkdir build
cd build
cmake ${PMEMFILE_SHARED_OPTS} -DCMAKE_BUILD_TYPE=Release
make install -j2
rm -r *
cmake ${PMEMFILE_SHARED_OPTS} -DCMAKE_BUILD_TYPE=Debug
make install -j2

export LD_LIBRARY_PATH=${PMEMFILE_LIB_DIR}:${LD_LIBRARY_PATH}

# Create pmemfile fs
$MKFS_PMEMFILE $PF_POOL 3G

# Run pmemfile testsuite short tests
set +e

$TEST_UTILS_DIR/run-suite.py $1 -i $SUITE_DIR -m $MOUNTPOINT -l $PMEMFILE_LIB_DIR \
	-p $PF_POOL -t $TESTS -f $FAILING_TESTS --timeout 120 $VERBOSE

if [ "$COVERAGE" = "1" ]; then
	bash <(curl -s https://codecov.io/bash)
fi

cd $WORKDIR
rm -rf build
rm -rf $TEST_DIR
