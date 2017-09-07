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

BIN_LOG=output-bin-1531-14.log
OUT=output-$TEST_NUM.log
ERR=output-err-$TEST_NUM.log
EXPECTED_RV=0

# test different options
case $TEST_NUM in
16)
	ANTOOL_OPTS="-f"
	;;
17)
	ANTOOL_OPTS="-m 10"
	;;
18)
	ANTOOL_OPTS="-m 10 -l"
	;;
19)
	ANTOOL_OPTS="-m 10 -l -s -f"
	;;
20)
	ANTOOL_OPTS="-m 10 -l -s -f -d"
	;;
21)
	# non-existing input file
	BIN_LOG=$(mktemp -u)
	EXPECTED_RV=255
	;;
esac

# copy input binary log to the test directory
[ -f ../$BIN_LOG ] && cp ../$BIN_LOG .

# following tests should fail
case $TEST_NUM in
22|23|24|25|26|27|28|29|30|31|36)
	EXPECTED_RV=255
	;;
esac

# truncate or corrupt the input binary log (should fail)
case $TEST_NUM in
22)	# truncate the log in the middle of the first signature
	truncate -s 10 $BIN_LOG
	;;
23)	# truncate the log after the architecture code
	truncate -s 28 $BIN_LOG
	;;
24)	# truncate the log one byte before the end of the first syscall table entry
	truncate -s 115 $BIN_LOG
	;;
25)	# truncate the log after the first syscall table entry
	truncate -s 116 $BIN_LOG
	;;
26)	# truncate the log one byte before the end of the log file
	ANTOOL_OPTS=""
	SIZE=$(stat -c %s $BIN_LOG)
	truncate -s $(($SIZE - 1)) $BIN_LOG
	;;
27)	# corrupt the signature of vltrace log
	truncate -s 10 $BIN_LOG
	# append 2 bytes
	echo -n "__" >> $BIN_LOG
	;;
28)	# corrupt the version of vltrace log (set it to 0.0.0)
	truncate -s 12 $BIN_LOG
	# append 12 null bytes
	echo -n -e '\x0\x0\x0\x0\x0\x0\x0\x0\x0\x0\x0\x0' >> $BIN_LOG
	;;
29)	# corrupt the architecture of vltrace log (set it to 0)
	truncate -s 24 $BIN_LOG
	# append 4 null bytes
	echo -n -e '\x0\x0\x0\x0' >> $BIN_LOG
	;;
30)	# corrupt the architecture of vltrace log (set it to very large number)
	truncate -s 24 $BIN_LOG
	# append 4 bytes
	echo -n "abcd" >> $BIN_LOG
	;;
31)	# corrupt the size of the syscalls table entry
	truncate -s 28 $BIN_LOG
	# append 4 bytes
	echo -n "abcd" >> $BIN_LOG
	;;
esac

# cut out or move packets
case $TEST_NUM in
32)	# move 2 packets to the end of log
	move_part_to_end $BIN_LOG 80755 80883
	ANTOOL_OPTS="-d"
	;;
33)	# move 2 packets to the end of log
	move_part_to_end $BIN_LOG 80755 80883
	ANTOOL_OPTS="-d -f"
	;;
34)	# cut out 2 packets from the log
	cut_out_from_file $BIN_LOG 80755 80883
	ANTOOL_OPTS="-d"
	;;
35)	# cut out 2 packets from the log
	cut_out_from_file $BIN_LOG 80755 80883
	ANTOOL_OPTS="-d -f"
	;;
36)	# move first packet of 'open' to the end of log
	move_part_to_end $BIN_LOG 88863 89331
	ANTOOL_OPTS="-d"
	;;
37)	# move first packet of 'open' to the end of log
	cut_out_from_file $BIN_LOG 80755 80883
	cut_out_from_file $BIN_LOG 149515 150919
	cut_out_from_file $BIN_LOG 181951 181995
	cut_out_from_file $BIN_LOG 216599 216643
	cut_out_from_file $BIN_LOG 220771 220815
	ANTOOL_OPTS="-d -f -l"
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
