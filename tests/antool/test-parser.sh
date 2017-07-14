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

# follow-fork option
if [ "$1" == "-f" ]; then
	FF="-f"
	shift
else
	FF=""
fi

if [ "$3" == "" ]; then
	echo "Usage: $0 [-f] <vltrace-path> <max-string-length> <test-file> <test-number>"
	echo "   -f - turn on follow-fork"
	exit 1
fi

VLTRACE=$1
MAX_STR_LEN=$2

[ ! -x $VLTRACE ] \
	&& echo "Error: executable file '$VLTRACE' does not exist" \
	&& exit 1

TEST_FILE=$3
TEST_NUM=$4

OPT_VLTRACE="-l bin -r -s $MAX_STR_LEN"
RUN_VLTRACE="ulimit -l 10240 && ulimit -n 10240 && $VLTRACE $OPT_VLTRACE"

if [ ! -x $TEST_FILE ]; then
	echo "Error: file '$TEST_FILE' does not exist or is not executable"
	exit 1
fi

TEST_DIR=$(dirname $0)
[ "$TEST_DIR" == "." ] && TEST_DIR=$(pwd)

ANTOOL=$TEST_DIR/../../src/tools/antool/antool.py

FUNCT=$TEST_DIR/helper_functions.sh
[ ! -f $FUNCT ] \
	&& echo "Error: missing file: $FUNCT" \
	&& exit 1

source $FUNCT

PATTERN_START="---------------- close 0000000012345678"
PATTERN_END="---------------- close 0000000087654321"

OUT_BIN=output-bin-$TEST_NUM.log
OUT_TXT=output-txt-$TEST_NUM.log
OUT_BARE=output-bare-$TEST_NUM.log
OUT_SORT=output-sort-$TEST_NUM.log

OUT_VLT=$OUT_BIN

if [ ! "$VLTRACE_SKIP" ]; then
	require_superuser
	# remove all logs of the current test
	rm -f *-$TEST_NUM.log
	echo "$ sudo bash -c \"$RUN_VLTRACE $FF -o $OUT_VLT $TEST_FILE $TEST_NUM\""
	sudo bash -c "$RUN_VLTRACE $FF -o $OUT_VLT $TEST_FILE $TEST_NUM"
fi

set +e
echo "$ $ANTOOL -c -s -t syscalls_table.dat -b $OUT_VLT > $OUT_TXT"
$ANTOOL -c -s -t syscalls_table.dat -b $OUT_VLT > $OUT_TXT
RV=$?
[ $RV -ne 0 ] \
	&& save_logs "*-$TEST_NUM.log" "match-$(basename $TEST_FILE)-$TEST_NUM" \
	&& exit $RV
grep --text -e "^0" $OUT_TXT > $OUT_BARE
set -e

[ $(cat $OUT_BARE | wc -l) -eq 0 ] \
	&& echo "ERROR: no traces in output of vltrace:" \
	&& cat $OUT_TXT \
	&& exit 1

if [ "$FF" == "" ]; then
	# tests without fork()
	echo "$ sort $OUT_BARE -o $OUT_SORT"
	sort $OUT_BARE -o $OUT_SORT
	cut_part_file $OUT_SORT "$PATTERN_START" "$PATTERN_END" > cut-$TEST_NUM.log
else
	# tests with fork()
	NFILES=$(split_forked_file $OUT_BARE out $TEST_NUM.log)
	for N in $(seq -s' ' $NFILES); do
		echo "$ sort out-$N-$TEST_NUM.log -o out-sort-$N-$TEST_NUM.log"
		sort out-$N-$TEST_NUM.log -o out-sort-$N-$TEST_NUM.log
		cut_part_file out-sort-$N-$TEST_NUM.log "$PATTERN_START" "$PATTERN_END" > cut-$N-$TEST_NUM.log
	done
fi

check_parser
