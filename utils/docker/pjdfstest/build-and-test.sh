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
# build-and-test.sh - builds pmemfile and runs pjdfstest
#

if [[ -z "$WORKDIR"  ]]; then
	echo "ERROR: The variable WORKDIR has to contain a path to " \
	"the root of pmemfile repository."
	exit 1
fi

export PMEMFILE_INSTALL_DIR=$HOME/pmemfile
export PMEMFILE_SHARED_OPTS=".. -DCMAKE_INSTALL_PREFIX=$PMEMFILE_INSTALL_DIR -DBUILD_LIBPMEMFILE_POSIX_TESTS=OFF -DBUILD_LIBPMEMFILE_TESTS=OFF -DLIBPMEMFILE_VALIDATE_POINTERS=ON"

if [ "$COVERAGE" = "1" ]; then
	export PMEMFILE_SHARED_OPTS="${PMEMFILE_SHARED_OPTS} -DCMAKE_C_FLAGS=-coverage -DCMAKE_CXX_FLAGS=-coverage"
fi

# Install pmemfile
cd $WORKDIR
mkdir -p build
cd build
if [ ! -d ${PMEMFILE_INSTALL_DIR} ]; then
	cmake ${PMEMFILE_SHARED_OPTS} -DCMAKE_BUILD_TYPE=Debug
	make install -j2
	rm -r *
	cmake ${PMEMFILE_SHARED_OPTS} -DCMAKE_BUILD_TYPE=RelWithDebInfo
	make install -j2
fi

export PMEMFILE_POOL_FILE=/tmp/pool
export PMEMFILE_POOL_SIZE=0
export PMEMFILE_MOUNT_POINT=/tmp/pjd
export PJDFSTEST_DIR=/root/pjdfstest

rm -f ${PMEMFILE_POOL_FILE}
truncate -s 1G ${PMEMFILE_POOL_FILE}
chmod a+rw ${PMEMFILE_POOL_FILE}

cd $WORKDIR/utils/docker/pjdfstest/
./test.sh

if [ "$COVERAGE" = "1" ]; then
	bash <(curl -s https://codecov.io/bash)
fi

cd $WORKDIR
rm -rf build
