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
# test-parser.sh -- test for antool using match and *.match files
#

#
# split_forked_file - split log files of forked processes
#                     split_forked_file <file> <name-part1> <name-part2>
#
function split_forked_file() {
	NAME1=$2
	NAME2=$3

	local INPUT=$(mktemp)
	local GREP=$(mktemp)

	cp $1 $INPUT

	local N=1
	local PID=$(head -n1 $INPUT | cut -d" " -f2)
	while true; do
		NAME="${NAME1}-${N}-${NAME2}"
		touch $NAME
		set +e
		grep -e "$PID" $INPUT > $NAME
		grep -v -e "$PID" $INPUT > $GREP
		cp $GREP $INPUT
		set -e
		[ $(cat $INPUT | wc -l) -eq 0 ] && break
		PID=$(head -n1 $INPUT | cut -d" " -f2)
		N=$(($N + 1))
	done
	rm -f $GREP $INPUT
	echo $N
}

NAME=$(basename $0)

# follow-fork option
if [ "$1" == "-f" ]; then
	FF="-f"
	shift
fi

if [ "$4" == "" ]; then
	echo "ERROR($NAME): not enough arguments"
	echo "Usage: $0 [-f] <vltrace-path> <max-string-length> <test-file> <test-number>"
	echo "   -f - turn on follow-fork"
	exit 1
fi

VLTRACE=$1
MAX_STR_LEN=$2
TEST_FILE=$3
TEST_NUM=$4

TEST_DIR=$(dirname $0)
[ "$TEST_DIR" == "." ] && TEST_DIR=$(pwd)

ANTOOL=$(realpath $TEST_DIR/../../src/tools/antool/antool.py)

COVERAGE_REPORT=.coverage
[ "$COVERAGE" == "1" ] && \
	ANTOOL="$(which python3) $(which coverage) run -a --rcfile=$TEST_DIR/.coveragerc --source=$PYTHON_SOURCE $ANTOOL"

FUNCT=$TEST_DIR/helper_functions.sh
[ ! -f $FUNCT ] \
	&& echo "Error: missing file: $FUNCT" \
	&& exit 1

source $FUNCT

# create a new temporary directory for the test to enable parallel testing
NAME_PATTERN="$NAME-$TEST_NUM-$MAX_STR_LEN"
DIR_NAME="logs-${NAME_PATTERN}-$(date +%F_%T_%N)-$$"
mkdir -p $DIR_NAME
cd $DIR_NAME

[ "$COVERAGE" == "1" -a -f ../$COVERAGE_REPORT ] && cp ../$COVERAGE_REPORT .

if [ "$VLTRACE" -a ! "$VLTRACE_SKIP" ]; then
	if [ ! -x $TEST_FILE ]; then
		echo "Error: file '$TEST_FILE' does not exist or is not executable"
		exit 1
	fi
	OPT_VLTRACE="$FF -l bin -r -s $MAX_STR_LEN"
	RUN_VLTRACE="ulimit -l 10240 && ulimit -n 10240 && PATH=\"$PATH\" $VLTRACE $OPT_VLTRACE"

else
	VLTRACE_SKIP=1
fi

PATTERN_START="------------------ close 0x0000000012345678"
PATTERN_END="------------------ close 0x0000000087654321"

SINGLE_TEST_NUM=$TEST_NUM
TEST_NUM=$MAX_STR_LEN-$TEST_NUM

OUT_BIN=output-bin-$TEST_NUM.log
OUT_TXT=output-txt-$TEST_NUM.log
OUT_ERR=output-err-$TEST_NUM.log
OUT_BARE=output-bare-$TEST_NUM.log
OUT_SORT=output-sort-$TEST_NUM.log

OUT_VLT=$OUT_BIN

if [ ! "$VLTRACE_SKIP" ]; then
	require_superuser
	# remove all old logs and match files of the current test
	rm -f *-$TEST_NUM.log*
	echo "$ sudo bash -c \"$RUN_VLTRACE $FF -o $OUT_VLT $TEST_FILE $SINGLE_TEST_NUM\""
	sudo bash -c "$RUN_VLTRACE $FF -o $OUT_VLT $TEST_FILE $SINGLE_TEST_NUM"
else
	cp ../$OUT_VLT .
fi

echo "$ $ANTOOL -c -s -b $OUT_VLT > $OUT_TXT 2> $OUT_ERR"
$ANTOOL -c -s -b $OUT_VLT > $OUT_TXT 2> $OUT_ERR

mv $OUT_TXT $OUT_BARE
[ $(cat $OUT_BARE | wc -l) -eq 0 ] \
	&& echo "ERROR: no traces in output of vltrace:" \
	&& cat $OUT_TXT \
	&& exit 1

sort $OUT_BARE -o $OUT_SORT

if [ ! "$FF" ]; then
	# tests without fork()
	cut_part_file $OUT_SORT "$PATTERN_START" "$PATTERN_END" > cut-$TEST_NUM.log
else
	# tests with fork()
	NFILES=$(split_forked_file $OUT_SORT out $TEST_NUM.log)
	for N in $(seq -s' ' $NFILES); do
		sort out-$N-$TEST_NUM.log -o out-sort-$N-$TEST_NUM.log
		cut_part_file out-sort-$N-$TEST_NUM.log "$PATTERN_START" "$PATTERN_END" > cut-$N-$TEST_NUM.log
	done
fi

check

# test succeeded
# copy vltrace binary log for regeneration
[ ! "$VLTRACE_SKIP" ] && mv -f $OUT_VLT ..

# copy coverage report
[ "$COVERAGE" == "1" -a -f $COVERAGE_REPORT ] && cp -f $COVERAGE_REPORT ..

# remove the temporary test directory
cd ..
rm -rf $DIR_NAME
