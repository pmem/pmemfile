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
# test-fi.sh -- fault injection tests for analyzing tool
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

function replace_n_bytes() { # file offset N new_bytes
	FILE=$1
	OFFSET=$(($2 - 1))
	N_BYTES=$3
	NEW_BYTES=$4
	OUTPUT=$(mktemp)

	SIZE1=$(stat -c %s $FILE)
	dd if=$FILE of=$OUTPUT  bs=$OFFSET count=1 status=noxfer 2>/dev/null
	# echo -n "Appending bytes: '$NEW_BYTES' == '" && echo -e $NEW_BYTES\'
	echo -n -e $NEW_BYTES >> $OUTPUT
	OFFSET=$(($OFFSET + $N_BYTES))
	dd if=$FILE ibs=$OFFSET skip=1 status=noxfer 2>/dev/null >> $OUTPUT
	mv $OUTPUT $FILE
	SIZE2=$(stat -c %s $FILE)
	if [ $SIZE1 -ne $SIZE2 ]; then
		echo "replace_n_bytes(): size mismatch: $SIZE1 => $SIZE2"
		exit 1
	fi
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

# test number (cut out the 'fi-' prefix)
NUM=$(echo $TEST_NUM | cut -d'-' -f2)

# copy input binary log to the test directory
[ -f ../$BIN_LOG ] && cp ../$BIN_LOG .

# following tests should fail
case $NUM in
6|7|8|9|10|11|12|13|14|15|16|21|28|32)
	EXPECTED_RV=255
	;;
esac

# truncate the log after the 10th packet
# in order to make logs and match files shorter
case $NUM in
17|18|19|20)
	truncate -s 82163 $BIN_LOG
	;;
esac

case $NUM in
#
# test different options
#
1)
	ANTOOL_OPTS="-m 10"
	;;
2)
	ANTOOL_OPTS="-m 10 -f"
	;;
3)
	ANTOOL_OPTS="-m 10 -l"
	;;
4)
	ANTOOL_OPTS="-m 10 -l -s -f"
	;;
5)
	ANTOOL_OPTS="-m 10 -l -s -f -d"
	;;
6)
	# non-existing input file
	BIN_LOG=$(mktemp -u)
	;;
#
# truncate or corrupt the input binary log (should fail)
#
7)	# truncate the log in the middle of the first signature
	truncate -s 10 $BIN_LOG
	;;
8)	# truncate the log after the architecture code
	truncate -s 28 $BIN_LOG
	;;
9)	# truncate the log one byte before the end of the first syscall table entry
	truncate -s 115 $BIN_LOG
	;;
10)	# truncate the log after the first syscall table entry
	truncate -s 116 $BIN_LOG
	;;
11)	# truncate the log one byte before the end of the 10th packet
	truncate -s 82162 $BIN_LOG
	;;
12)	# corrupt the signature of vltrace log
	truncate -s 10 $BIN_LOG
	# append 2 bytes
	echo -n "__" >> $BIN_LOG
	;;
13)	# corrupt the version of vltrace log (set it to 0.0.0)
	truncate -s 12 $BIN_LOG
	# append 12 null bytes
	echo -n -e '\x0\x0\x0\x0\x0\x0\x0\x0\x0\x0\x0\x0' >> $BIN_LOG
	;;
14)	# corrupt the architecture of vltrace log (set it to 0)
	truncate -s 24 $BIN_LOG
	# append 4 null bytes
	echo -n -e '\x0\x0\x0\x0' >> $BIN_LOG
	;;
15)	# corrupt the architecture of vltrace log (set it to very large number)
	truncate -s 24 $BIN_LOG
	# append 4 bytes
	echo -n "abcd" >> $BIN_LOG
	;;
16)	# corrupt the size of the syscalls table entry
	truncate -s 28 $BIN_LOG
	# append 4 bytes
	echo -n "abcd" >> $BIN_LOG
	;;
#
# cut out or move packets
#
17)	# move 2 packets to the end of log
	move_part_to_end $BIN_LOG 80755 80883
	ANTOOL_OPTS="-d"
	;;
18)	# move 2 packets to the end of log
	move_part_to_end $BIN_LOG 80755 80883
	ANTOOL_OPTS="-d -f"
	;;
19)	# cut out 2 packets from the log
	cut_out_from_file $BIN_LOG 80755 80883
	ANTOOL_OPTS="-d"
	;;
20)	# cut out 2 packets from the log
	cut_out_from_file $BIN_LOG 80755 80883
	ANTOOL_OPTS="-d -f"
	;;
21)	# move first packet of 'open' to the end of log
	move_part_to_end $BIN_LOG 88863 89331
	ANTOOL_OPTS="-d"
	;;
22)	# cut out some packets from the log in order to get
	# the 'list_no_exit' and 'list_others' lists not empty
	cut_out_from_file $BIN_LOG 80755 80883
	cut_out_from_file $BIN_LOG 149515 150919
	cut_out_from_file $BIN_LOG 181951 181995
	cut_out_from_file $BIN_LOG 216599 216643
	cut_out_from_file $BIN_LOG 220771 220815
	ANTOOL_OPTS="-d -f -l"
	;;
23)	# zero pid_tid of packet in STATE_IN_ENTRY state of SyS_open
	replace_n_bytes $BIN_LOG 89340 8 '\x0\x0\x0\x0\x0\x0\x0\x0'
	# change 1st byte of path of openat to '/' to make it an absolute path
	replace_n_bytes $BIN_LOG 90860 1 '\x2F'
	# change 1st byte of path of execve to null to make it an empty path
	replace_n_bytes $BIN_LOG 80368 1 '\x00'
	# set file descriptor of newfstat to 0x00000000F0FFFFFF
	replace_n_bytes $BIN_LOG 82112 4 '\xFF\xFF\xFF\xF0'
	ANTOOL_OPTS="-s -d"
	;;
24)	# inject BPF read error in the 1st packet of open (offset 81567)
	replace_n_bytes $BIN_LOG 81573 1 '\x4' # offset +6
	replace_n_bytes $BIN_LOG 81648 1 '\x0' # offset +81
	ANTOOL_OPTS="-m 12 -s -d"
	;;
25)	# inject BPF read error in the 1st packet of open (offset 81567)
	replace_n_bytes $BIN_LOG 81573 1 '\x4' # offset +6
	replace_n_bytes $BIN_LOG 81648 1 '\x0' # offset +81
	ANTOOL_OPTS="-m 12 -s -f -l"
	;;
26)	# change size of SyS_open packet (offset 81567)
	replace_n_bytes $BIN_LOG 81568 2 '\x4B\x00'
	ANTOOL_OPTS="-m 9 -s -d"
	;;
27)	# change size of SyS_fork packet (offset 87711)
	replace_n_bytes $BIN_LOG 87712 2 '\x4B\x00'
	ANTOOL_OPTS="-m 87 -s"
	;;
28)	# change size of SyS_open packet (offset 89331)
	replace_n_bytes $BIN_LOG 89332 2 '\x4B\x00'
	ANTOOL_OPTS="-s -d"
	;;
29)	# change syscall in packet #14 from close to dup
	replace_n_bytes $BIN_LOG 82352 1 '\x20'
	# change syscall in packet #15 from close to dup
	replace_n_bytes $BIN_LOG 82436 1 '\x20'
	# change fd in 1st argument to 0x12345678
	replace_n_bytes $BIN_LOG 82368 4 '\x78\x56\x34\x12'
	# change return value to -1
	replace_n_bytes $BIN_LOG 82452 8 '\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF'
	ANTOOL_OPTS="-m16 -s -d"
	;;
30)	# change syscall in packet #14 from close to dup
	replace_n_bytes $BIN_LOG 82352 1 '\x20'
	# change syscall in packet #15 from close to dup
	replace_n_bytes $BIN_LOG 82436 1 '\x20'
	# change fd in 1st argument to 0x12345678
	replace_n_bytes $BIN_LOG 82368 4 '\x78\x56\x34\x12'
	ANTOOL_OPTS="-m16 -s -d"
	;;
31)	# change syscall in packet #14 from close to dup
	replace_n_bytes $BIN_LOG 82352 1 '\x20'
	# change syscall in packet #15 from close to dup
	replace_n_bytes $BIN_LOG 82436 1 '\x20'
	# change return value to -1
	replace_n_bytes $BIN_LOG 82452 8 '\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF'
	ANTOOL_OPTS="-m16 -s -d"
	;;
32)	# change return value of openat to 0x12345678 (packet #107)
	replace_n_bytes $BIN_LOG 92684 8 '\x00\x00\x00\x00\x78\x56\x34\x12'
	ANTOOL_OPTS="-m108 -s -d"
	;;
33)	# change syscall in packet #14 from close to fcntl
	replace_n_bytes $BIN_LOG 82352 1 '\x48'
	# change syscall in packet #15 from close to fcntl
	replace_n_bytes $BIN_LOG 82436 1 '\x48'
	# change fcntl's flag to F_GETOWN (0x09) (2nd argument)
	replace_n_bytes $BIN_LOG 82376 4 '\x09\x00\x00\x00'
	ANTOOL_OPTS="-m16 -s -d -p /etc"
	;;
34)	# change syscall in packet #14 from close to fallocate
	replace_n_bytes $BIN_LOG 82352 2 '\x1D\x01'
	# change syscall in packet #15 from close to fallocate
	replace_n_bytes $BIN_LOG 82436 2 '\x1D\x01'
	# change fallocate's argument #2
	replace_n_bytes $BIN_LOG 82376 4 '\x00\x00\x00\x00'
	# change fallocate's argument #3
	replace_n_bytes $BIN_LOG 82384 4 '\x00\x00\x00\x00'
	# change fallocate's argument #4
	replace_n_bytes $BIN_LOG 82392 4 '\x00\x00\x00\x00'
	ANTOOL_OPTS="-m16 -s -d -p /etc"
	;;
35)	# change syscall in packet #14 from close to SyS_symlinkat
	replace_n_bytes $BIN_LOG 82352 2 '\x0A\x01'
	# change syscall in packet #15 from close to SyS_symlinkat
	replace_n_bytes $BIN_LOG 82436 2 '\x0A\x01'
	# change SyS_symlinkat argument #2 to AT_FDCWD
	replace_n_bytes $BIN_LOG 82376 4 '\x9C\xFF\xFF\xFF'
	ANTOOL_OPTS="-m16 -s -d -p /etc"
	;;
36)	# change syscall in packet #14 from close to SyS_fstatat
	replace_n_bytes $BIN_LOG 82352 2 '\x06\x01'
	# change syscall in packet #15 from close to SyS_fstatat
	replace_n_bytes $BIN_LOG 82436 2 '\x06\x01'
	# change fallocate's argument #4
	replace_n_bytes $BIN_LOG 82392 4 '\x00\x10\x00\x00'
	ANTOOL_OPTS="-m16 -s -d -p /etc"
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
[ "$COVERAGE" == "1" -a -f $COVERAGE_REPORT ] && mv -f $COVERAGE_REPORT ..

# remove the temporary test directory
if [ "$(basename $(pwd))" == "$DIR_NAME" ]; then
	cd ..
	rm ./$DIR_NAME/*
	rmdir ./$DIR_NAME
fi
