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
# test-antool.sh -- test for analyzing tool
#

NAME=$(basename $0)

# follow-fork option
if [ "$1" == "-f" ]; then
	FF="-f"
	shift
fi

if [ "$4" == "" ]; then
	echo "ERROR($NAME): not enough arguments"
	echo "Usage: $0 [-f] <path-to-vltrace> <max-string-length> <test-app> <test-number> [extra-antool-options]"
	echo "   -f                      -  turn on follow-fork"
	echo "   <max-string-length>     -  parameter of vltrace (see the man page of vltrace)"
	echo "   [extra-antool-options]  -  extra options for antool (optional)"
	exit 1
fi

VLTRACE=$1
MAX_STR_LEN=$2
TEST_FILE=$3
TEST_NUM=$4
EXTRA_ANTOOL_OPTIONS=$5

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

SINGLE_TEST_NUM=$TEST_NUM
TEST_NUM=$MAX_STR_LEN-$TEST_NUM
FILE_DIR_PMEM="dir_pmem-$TEST_NUM.txt"

[ "$COVERAGE" == "1" -a -f ../$COVERAGE_REPORT ] && cp ../$COVERAGE_REPORT .

if [ "$VLTRACE" -a ! "$VLTRACE_SKIP" ]; then
	if [ ! -x $TEST_FILE ]; then
		echo "Error: executable file '$TEST_FILE' does not exist"
		exit 1
	fi
	OPT_VLTRACE="$FF -l bin -t -r -s $MAX_STR_LEN"
	RUN_VLTRACE="ulimit -l 10240 && ulimit -n 10240 && PATH=\"$PATH\" $VLTRACE $OPT_VLTRACE"
else
	if [ ! "$VLTRACE_SKIP" -a -f ../$FILE_DIR_PMEM ]; then
		cp ../$FILE_DIR_PMEM .
	fi
	if [ ! -f $FILE_DIR_PMEM ]; then
		echo "Error: path to vltrace is not set and the file containing"\
			"path to pmem does not exist ($FILE_DIR_PMEM)"
		exit 1
	fi
	VLTRACE_SKIP=$(cat $FILE_DIR_PMEM)
	if [ "$VLTRACE_SKIP" == "" ]; then
		echo "Error: path to vltrace is not set and the file containing"\
			"path to pmem is empty ($FILE_DIR_PMEM)"
		exit 1
	fi
fi

PATTERN_START="close                (0x0000000012345678)"
PATTERN_END="close                (0x0000000087654321)"

OUTBIN=output-bin-$TEST_NUM.log
OUT=output-$TEST_NUM.log
OUTv=output-v-$TEST_NUM.log
OUTvv=output-vv-$TEST_NUM.log
OUTf=output-f-$TEST_NUM.log
OUTfv=output-fv-$TEST_NUM.log
OUTfvv=output-fvv-$TEST_NUM.log
OUT_ANTOOL=output-antool-$TEST_NUM.log
OUTda=output-analysis-$TEST_NUM.log

if [ ! "$VLTRACE_SKIP" ]; then
	require_superuser

	DIR=$(mktemp -d -p /tmp "antool XXX")
	DIR_PMEM=$(mktemp -d -p "$DIR" "pmem XXX")
	DIR_NONP=$(mktemp -d -p "$DIR" "nonp XXX")
	FILE_PMEM=$(mktemp -p "$DIR_PMEM" "tmp XXX")
	FILE_NONP=$(mktemp -p "$DIR_NONP" "tmp XXX")
	FILE_PMEM=$(echo "$FILE_PMEM" | cut -d"/" -f4-)
	FILE_NONP=$(echo "$FILE_NONP" | cut -d"/" -f4-)
	TEST_OPTIONS="\"$DIR\" \"$FILE_PMEM\" \"$FILE_NONP\""
	echo "$DIR_PMEM" > $FILE_DIR_PMEM

	USER=$(stat --format=%U $TEST_FILE)
	chown -R $USER.$USER "$DIR"/*

	# remove all old logs and match files of the current test
	rm -f *-$TEST_NUM.log*
	echo "$ sudo bash -c \"$RUN_VLTRACE -o $OUTBIN $TEST_FILE $SINGLE_TEST_NUM $TEST_OPTIONS\""
	sudo bash -c "$RUN_VLTRACE -o $OUTBIN $TEST_FILE $SINGLE_TEST_NUM $TEST_OPTIONS"
else
	DIR_PMEM=$VLTRACE_SKIP
	cp ../$OUTBIN .
fi

COMMON_OPTS="-b $OUTBIN -s -o $OUT_ANTOOL $EXTRA_ANTOOL_OPTIONS"
ANTOOL="$ANTOOL $COMMON_OPTS"

echo "$ $ANTOOL -p "$DIR_PMEM" > $OUT"
$ANTOOL -p "$DIR_PMEM" > $OUT

echo "$ $ANTOOL -p "$DIR_PMEM" -v > $OUTv"
$ANTOOL -p "$DIR_PMEM" -v > $OUTv

echo "$ $ANTOOL -p "$DIR_PMEM" -vv > $OUTvv"
$ANTOOL -p "$DIR_PMEM" -vv > $OUTvv

echo "$ $ANTOOL -p "$DIR_PMEM" -f > $OUTf"
$ANTOOL -p "$DIR_PMEM" -f > $OUTf

echo "$ $ANTOOL -p "$DIR_PMEM" -f -v > $OUTfv"
$ANTOOL -p "$DIR_PMEM" -f -v > $OUTfv

echo "$ $ANTOOL -p "$DIR_PMEM" -f -vv > $OUTfvv"
$ANTOOL -p "$DIR_PMEM" -f -vv > $OUTfvv

if [ ! "$FF" ]; then
	rm -f $OUT_ANTOOL

	echo "$ $ANTOOL -p "$DIR_PMEM" -d > /dev/null"
	$ANTOOL -p "$DIR_PMEM" -d > /dev/null

	grep -e "DEBUG(analysis):" $OUT_ANTOOL | cut -c18- | grep -v -e "DEBUG" > $OUTda
	cut_part_file $OUTda "$PATTERN_START" "$PATTERN_END" > cut-$TEST_NUM.log
fi

check

# test succeeded
# copy vltrace binary log for regeneration
if [ ! "$VLTRACE_SKIP" ]; then
	mv -f $OUTBIN ..
	mv -f $FILE_DIR_PMEM ..
fi

# copy coverage report
[ "$COVERAGE" == "1" -a -f $COVERAGE_REPORT ] && cp -f $COVERAGE_REPORT ..

# remove the temporary test directory
cd ..
rm -rf $DIR_NAME
