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
	echo "Usage: $NAME <all|fi_only> <path-to-test-source-directory> <destination-directory>"
	echo "      where:"
	echo "            - all      -  all binary logs"
	echo "            - fi_only  -  only one fault-injection binary log"
	exit 1
fi

MODE=$1
SRC_DIR=$2
DEST_DIR=$3

TEST_DIR=$(dirname $0)
[ "$TEST_DIR" == "." ] && TEST_DIR=$(pwd)

COMMON=$TEST_DIR/common.sh
[ ! -f $COMMON ] \
	&& echo "Error: missing file: $COMMON" \
	&& exit 1

source $COMMON

if [ "$MODE" == "fi_only" ]; then
	FILES_ARCH="$SRC_DIR/$FI_SRC_BIN_LOG.$ARCH_EXT $SRC_DIR/$SYSCALL_TABLE.$ARCH_EXT"
else
	FILES_ARCH=$(ls -1 $SRC_DIR/$ARCHIVES 2>/dev/null)
fi

if [ "$FILES_ARCH" == "" ]; then
	echo "ERROR: no archives found"
	exit 1
fi

echo -n "Decompressing $MODE vltrace's binary logs... "

for file in $FILES_ARCH; do
	bzip2 -d -f -k $file
done

TEMP_FILE=$(mktemp)

# concatenate log files with syscall table
for file in $SRC_DIR/$MASK_BIN; do
	cp $file $TEMP_FILE
	cp $SRC_DIR/$SYSCALL_TABLE $file
	cat $TEMP_FILE >> $file
done

rm -f $TEMP_FILE

echo "done."

echo -n "-- Moving binary logs to the test directory... "
if [ "$MODE" == "fi_only" ]; then
	mv -f $SRC_DIR/$FI_SRC_BIN_LOG $SRC_DIR/$FI_BIN_LOG
else
	cp -f $SRC_DIR/$FI_SRC_BIN_LOG $SRC_DIR/$FI_BIN_LOG
	cp -f $SRC_DIR/$MASK_DIR_PMEM $DEST_DIR
fi
mv -f $SRC_DIR/$MASK_BIN $DEST_DIR
rm -f $SRC_DIR/$SYSCALL_TABLE
echo "done."
