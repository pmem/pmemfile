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
# test.sh
#

if [ -z "${PMEMFILE_INSTALL_DIR}" ]; then
	echo "PMEMFILE_INSTALL_DIR not set"
	exit 1
fi

if [ -z "${PMEMFILE_POOL_FILE}" ]; then
	echo "PMEMFILE_POOL_FILE not set"
	exit 1
fi

if [ -z "${PMEMFILE_POOL_SIZE}" ]; then
	echo "PMEMFILE_POOL_SIZE not set"
	exit 1
fi

if [ -z "${PMEMFILE_MOUNT_POINT}" ]; then
	echo "PMEMFILE_MOUNT_POINT not set"
	exit 1
fi

if [ -z "${PJDFSTEST_DIR}" ]; then
	echo "PJDFSTEST_DIR not set"
	exit 1
fi

export TEST_DIR=`pwd`
export LD_LIBRARY_PATH=${PMEMFILE_INSTALL_DIR}/lib:${LD_LIBRARY_PATH}
export PATH=${PMEMFILE_INSTALL_DIR}/bin:${PATH}
export PMEMFILE_PRELOAD_PROCESS_SWITCHING=1
export PMEMOBJ_CONF="tx.debug.skip_expensive_checks=1"

mkdir -p ${PMEMFILE_MOUNT_POINT}
mount -t tmpfs | grep "pmemfile:${PMEMFILE_POOL_FILE} on ${PMEMFILE_MOUNT_POINT} " && umount ${PMEMFILE_MOUNT_POINT}

if echo "${PMEMFILE_POOL_FILE}" | grep -q "^/dev/dax"; then
	pmempool rm ${PMEMFILE_POOL_FILE}
fi

mkfs.pmemfile ${PMEMFILE_POOL_FILE} ${PMEMFILE_POOL_SIZE}
pmemfile-mount ${PMEMFILE_POOL_FILE} ${PMEMFILE_MOUNT_POINT}
#export PMEMFILE_POOLS=${PMEMFILE_MOUNT_POINT}:${PMEMFILE_POOL_FILE}
export PMEM_IS_PMEM_FORCE=1

cd ${PMEMFILE_MOUNT_POINT}

rm -f ${TEST_DIR}/pmemfile.log
LD_PRELOAD=libpmemfile.so prove -rv --timer ${PJDFSTEST_DIR}/tests/$1 2>&1 | tee ${TEST_DIR}/pmemfile.log

cd ${TEST_DIR}
cat pmemfile.log | sed -n "/Test Summary Report/,\$p" > pmemfile-summary.log
../../../tests/match pmemfile-summary.log.match
