#!/bin/bash
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

LOG_OFFSET=80036

NAME=$(basename $0)

if [ "$1" == "" ]; then
	echo "ERROR($NAME): not enough arguments"
	echo "Usage: $NAME <path-to-test-source-directory>"
	exit 1
fi

SRC_DIR=$1

TEST_DIR=$(dirname $0)
[ "$TEST_DIR" == "." ] && TEST_DIR=$(pwd)

COMMON=$TEST_DIR/common.sh
[ ! -f $COMMON ] \
	&& echo "Error: missing file: $COMMON" \
	&& exit 1

source $COMMON

FILES_BIN=$(ls -1 $MASK_BIN 2>/dev/null)
if [ "$FILES_BIN" == "" ]; then
	echo "ERROR: no binary logs found in the test directory: $(pwd)"
	echo "       - please rerun antool tests"
	exit 1
fi

if [ $(ls -1 $MASK_DIR_PMEM | wc -l) -eq 0 ]; then
	echo "ERROR: files with pmem directory do not exist: $MASK_DIR_PMEM"
	echo "       - please rerun antool tests"
	exit 1
fi

cp -f $MASK_DIR_PMEM $SRC_DIR

rm -f *.$ARCH_EXT

echo -n "Regenerating archives... "
TEMP_FILE=$(mktemp)
rm -f $SYSCALL_TABLE

for file in $FILES_BIN; do
	# cut the syscall table out of the first log file
	[ ! -f $SYSCALL_TABLE ] \
		&& dd if=$file of=$SYSCALL_TABLE iflag=count_bytes count=$LOG_OFFSET status=none

	mv $file $TEMP_FILE

	# cut off the syscall table at the beginning of the log file
	dd if=$TEMP_FILE of=$file iflag=skip_bytes skip=$LOG_OFFSET status=none

	# compress the cut log file
	bzip2 -f -k -9 $file

	mv $TEMP_FILE $file
done

bzip2 -f -k -9 $SYSCALL_TABLE
rm -f $SYSCALL_TABLE
echo "done."

echo "Regenerated following archives... "
ls -alh $ARCHIVES
echo

echo -n "Moving generated archives to the source directory... "
mv -f $ARCHIVES $SRC_DIR
echo "done."
