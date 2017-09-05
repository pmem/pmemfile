#!/bin/bash -e
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
#
# test-others.sh -- other test for analyzing tool
#

function cut_out_from_file() { # file offset1 offset2
	FILE=$1
	OFFSET_1=$2
	OFFSET_2=$3
	OUTPUT=$(mktemp)
	dd if=$FILE of=$OUTPUT  bs=$OFFSET_1 count=1 status=noxfer 2>/dev/null
	dd if=$FILE of=$OUTPUT ibs=$OFFSET_2 skip=1 obs=$OFFSET_1 seek=1 status=noxfer 2>/dev/null
	mv $OUTPUT $FILE
}

function move_part_to_end() { # file offset1 offset2
	FILE=$1
	OFFSET_1=$2
	OFFSET_2=$3
	OUTPUT=$(mktemp)
	dd if=$FILE of=$OUTPUT  bs=$OFFSET_1 count=1 status=noxfer 2>/dev/null
	dd if=$FILE of=$OUTPUT ibs=$OFFSET_2 skip=1 obs=$OFFSET_1 seek=1 status=noxfer 2>/dev/null
	dd if=$FILE ibs=1 skip=$OFFSET_1 count=$(($OFFSET_2 - $OFFSET_1)) status=noxfer 2>/dev/null >> $OUTPUT
	mv $OUTPUT $FILE
}

NAME=$(basename $0)

if [ "$1" == "" ]; then
	echo "ERROR($NAME): not enough arguments"
	echo "Usage: $0 <test-number>"
	exit 1
fi

TEST_NUM=$1

TEST_DIR=$(dirname $0)
[ "$TEST_DIR" == "." ] && TEST_DIR=$(pwd)

COMMON=$TEST_DIR/common.sh
[ ! -f $COMMON ] \
	&& echo "Error: missing file: $COMMON" \
	&& exit 1

source $COMMON

FUNCT=$TEST_DIR/helper_functions.sh
[ ! -f $FUNCT ] \
	&& echo "Error: missing file: $FUNCT" \
	&& exit 1

source $FUNCT

ANTOOL=$(realpath $TEST_DIR/../../src/tools/antool/antool.py)

COVERAGE_REPORT=.coverage
[ "$COVERAGE" == "1" ] && \
	ANTOOL="$(which python3) $(which coverage) run -a --rcfile=$TEST_DIR/.coveragerc --source=$PYTHON_SOURCE $ANTOOL"

# create a new temporary directory for the test to enable parallel testing
NAME_PATTERN="$NAME-$TEST_NUM"
DIR_NAME="logs-${NAME_PATTERN}-$(date +%F_%T_%N)-$$"
mkdir -p $DIR_NAME
cd $DIR_NAME

[ "$COVERAGE" == "1" -a -f ../$COVERAGE_REPORT ] && cp ../$COVERAGE_REPORT .

# let's take output-bin-7.log as an input log, because it is the smallest one
BIN_LOG=output-bin-7.log

OUT=output-$TEST_NUM.log
ERR=output-err-$TEST_NUM.log
EXPECTED_RV=0

case $TEST_NUM in
16)
	ANTOOL_OPTS="-m 1000"
	;;
17)
	ANTOOL_OPTS="-m 10"
	;;
18)
	ANTOOL_OPTS="-l"
	;;
19)
	ANTOOL_OPTS="-l -s -f"
	;;
20)
	ANTOOL_OPTS="-l -s -f -d"
	;;
21)
	# non-existing input file
	BIN_LOG=$(mktemp -u)
	EXPECTED_RV=255
	;;
22|23|24|25|26|27|28|29|30|31)
	# input binary log will be truncated
	EXPECTED_RV=255
	;;
esac

[ -f ../$BIN_LOG ] && cp ../$BIN_LOG .

# truncate input binary log
case $TEST_NUM in
22)
	truncate -s 10 $BIN_LOG
	;;
23)
	truncate -s 28 $BIN_LOG
	;;
24)
	truncate -s 115 $BIN_LOG
	;;
25)
	truncate -s 116 $BIN_LOG
	;;
26)
	# original file size = 88553
	truncate -s 88552 $BIN_LOG
	;;
27)	# wrong signature of vltrace log
	truncate -s 10 $BIN_LOG
	# append 2 bytes
	echo -n "__" >> $BIN_LOG
	;;
28)	# wrong version of vltrace log
	truncate -s 12 $BIN_LOG
	# append 12 null bytes
	echo -n -e '\x0\x0\x0\x0\x0\x0\x0\x0\x0\x0\x0\x0' >> $BIN_LOG
	;;
29)	# wrong architecture (0) of vltrace log
	truncate -s 24 $BIN_LOG
	# append 4 bytes
	echo -n -e '\x0\x0\x0\x0' >> $BIN_LOG
	;;
30)	# wrong architecture (very large number) of vltrace log
	truncate -s 24 $BIN_LOG
	# append 4 bytes
	echo -n "abcd" >> $BIN_LOG
	;;
31)	# wrong format of syscalls table
	truncate -s 28 $BIN_LOG
	# append 4 bytes
	echo -n "abcd" >> $BIN_LOG
	;;
32)	# move 2 packets to the end of log
	move_part_to_end $BIN_LOG 80745 80873
	ANTOOL_OPTS="-d -l"
	;;
33)	# move 2 packets to the end of log
	move_part_to_end $BIN_LOG 80745 80873
	ANTOOL_OPTS="-d -l -f"
	;;
34)	# cut out 2 packets from the log
	cut_out_from_file $BIN_LOG 80745 80873
	ANTOOL_OPTS="-d -l"
	;;
35)	# cut out 2 packets from the log
	cut_out_from_file $BIN_LOG 80745 80873
	ANTOOL_OPTS="-d -l -f"
	;;
esac

ANTOOL_OPTS="-b $BIN_LOG $ANTOOL_OPTS"

set +e
echo "$ $ANTOOL $ANTOOL_OPTS > $OUT 2> $ERR"
$ANTOOL $ANTOOL_OPTS > $OUT 2> $ERR
RV=$?
set -e

if [ $RV -ne $EXPECTED_RV ]; then
	echo "Error: return value ($RV) different than expected ($EXPECTED_RV)"
	exit 1
fi

check

# test succeeded

# copy coverage report
[ "$COVERAGE" == "1" -a -f $COVERAGE_REPORT ] && cp -f $COVERAGE_REPORT ..

# remove the temporary test directory
cd ..
rm -rf $DIR_NAME
