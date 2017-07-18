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
# require_superuser -- require superuser capabilities
#
function require_superuser() {
	local user_id=$(sudo -n id -u)
	[ "$user_id" == "0" ] && return
	echo "Superuser rights required, please enter root's password:"
	sudo date > /dev/null
	[ $? -eq 0 ] && return
	echo "Authentication failed, aborting..."
	exit 1
}

#
# save_logs -- save all logs of the current test
#
# usage: save_logs <file-mask> <name-pattern>
#
function save_logs() {
	FILE_MASK=$1
	NAME_PATTERN=$2
	if [ "${VLTRACE_TEST_SAVE_LOGS}" ]; then
		DIR_NAME="logs-${NAME_PATTERN}-$(date +%F_%T_%N)-$$"
		mkdir $DIR_NAME
		cp $FILE_MASK $DIR_NAME/
		echo "NOTICE: all log files were saved in the directory: $DIR_NAME"
	fi
}

#
# check_result_save_logs -- check result and save logs
#
# usage: check_result_save_logs <result> <name-pattern>
#
function check_result_save_logs() {
	RV=$1
	[ $RV -ne 0 ] \
		&& save_logs "*-$TEST_NUM.log" "$2" \
		&& exit $RV
}

#
# get_line_of_pattern -- get a line number of the first pattern in the file
#                        get_line_of_pattern <file> <pattern>
#
function get_line_of_pattern() {
	grep -n -e "$2" $1 | cut -d: -f1 | head -n1
}

#
# cut_part_file -- cut part of the file $1
#                  starting from the pattern $2
#                  ending at the pattern $3
#
function cut_part_file() {

	local FILE=$1
	local PATTERN1=$2
	local PATTERN2=$3

	local LINE1=$(get_line_of_pattern $FILE "$PATTERN1")
	[ "$LINE1" == "" ] \
		&& echo "ERROR: cut_part_file(): the start-pattern \"$PATTERN1\" not found in file $FILE" >&2 \
		&& return

	local LINE2=$(get_line_of_pattern $FILE "$PATTERN2")
	[ "$LINE2" == "" ] \
		&& LINE2=$(cat $FILE | wc -l) # print the file till the end

	sed -n ${LINE1},${LINE2}p $FILE
}

#
# check -- check test results (using .match files)
#
function check() {
	# copy match files
	[ "$TEST_DIR" != "$(pwd)" ] && cp -v -f $TEST_DIR/*-${TEST_NUM}.log.match .

	# run 'match'
	$TEST_DIR/../match *-${TEST_NUM}.log.match
}
