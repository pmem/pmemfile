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

NAME=$(basename $0)

if [ "$2" == "" ]; then
	echo "ERROR($NAME): not enough arguments"
	echo "Usage: $NAME <path-to-test-source-directory> <destination-directory>"
	exit 1
fi

SRC_DIR=$1
DEST_DIR=$2

TEST_DIR=$(dirname $0)
[ "$TEST_DIR" == "." ] && TEST_DIR=$(pwd)

COMMON=$TEST_DIR/common.sh
[ ! -f $COMMON ] \
	&& echo "Error: missing file: $COMMON" \
	&& exit 1

source $COMMON

FILES_ARCH=$(ls -1 $SRC_DIR/$ARCHIVES 2>/dev/null)
if [ "$FILES_ARCH" == "" ]; then
	echo "ERROR: no archives found"
	exit 1
fi

echo -n "Decompressing vltrace's binary logs... "
for file in $FILES_ARCH; do
	bunzip2 -f -k $file
done
echo "done."

echo "Moving binary logs to the test directory... "
mv -f -v $SRC_DIR/$MASK_BIN $DEST_DIR
mv -f -v $SRC_DIR/$SYSCALL_TABLE $DEST_DIR
cp -f -v $SRC_DIR/$FILE_DIR_PMEM $DEST_DIR
echo "done."
