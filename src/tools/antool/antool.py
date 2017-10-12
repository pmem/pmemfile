#!/usr/bin/python3
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

import argparse

from analyzer import *


########################################################################################################################
# main
########################################################################################################################

def main():
    parser = argparse.ArgumentParser(
                        description="Analyzing Tool - analyze binary logs of vltrace "
                                    "and check if all recorded syscalls are supported by pmemfile")

    parser.add_argument("-b", "--binlog", required=True, help="path to a vltrace log in binary format")

    parser.add_argument("-c", "--convert", action='store_true', required=False,
                        help="converter mode - only converts vltrace log from binary to text format")

    parser.add_argument("-p", "--pmem", required=False, help="paths to colon-separated pmem filesystems")
    parser.add_argument("-m", "--max-packets", required=False,
                        help="maximum number of packets to be read from the vltrace binary log")

    parser.add_argument("-l", "--log", action='store_true', required=False, help="print converted log in analyze mode")
    parser.add_argument("-o", "--output", required=False, help="file to save analysis output")

    parser.add_argument("-s", "--script", action='store_true', required=False,
                        help="script mode - print only the most important information (eg. no info about progress)")
    parser.add_argument("-v", "--verbose", action='count', required=False,
                        help="verbose mode (-v: verbose, -vv: very verbose)")
    parser.add_argument("-d", "--debug", action='store_true', required=False, help="debug mode")
    parser.add_argument("-f", "--offline", action='store_true', required=False, help="offline analysis mode")

    parser.add_argument("-t", "--print-all-syscalls", action='store_true', required=False,
                        help="print also syscalls without path or file descriptor among arguments to the analysis log")

    parser.add_argument("--slink-file", required=False, help="resolve symlinks saved in the given file")

    args = parser.parse_args()
    print_all = args.print_all_syscalls

    if args.verbose:
        verbose = args.verbose
    else:
        verbose = 0

    at = Analyzer(args.convert, args.pmem, args.slink_file, args.output, args.max_packets, args.offline,
                  args.script, args.debug, args.log, verbose, print_all)

    at.read_and_parse_data(args.binlog)

    if not args.convert and not args.offline:
        at.analyse_and_print_unsupported_online()

    if args.offline and (args.convert or args.log):
        at.print_log()
        if args.debug:
            at.print_other_lists()

    if args.convert or not args.offline:
        return

    at.set_pid_index_offline()
    at.match_fd_with_path_offline()
    at.print_unsupported_syscalls_offline()


if __name__ == "__main__":  # pragma: no cover
    main()
