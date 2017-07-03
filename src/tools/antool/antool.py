#!/usr/bin/python3
#
# Copyright (c) 2017, Intel Corporation
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
#     * Neither the name of Intel Corporation nor the names of its
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

from sys import exc_info, stderr, stdout
import argparse

from syscalltable import *
from listsyscalls import *

DO_REINIT = 0
DO_CONTINUE = 1
DO_GO_ON = 2

###############################################################################
# AnalyzingTool
###############################################################################
class AnalyzingTool(ListSyscalls):
    def __init__(self, convert_mode, pmem_paths, script_mode, debug_mode,
                    fileout, max_packets, verbose_mode, offline_mode):
        self.convert_mode = convert_mode
        self.script_mode = script_mode
        self.debug_mode = debug_mode
        self.offline_mode = offline_mode
        if verbose_mode:
            self.verbose_mode = verbose_mode
        else:
            self.verbose_mode = 0

        self.cwd = ""
        self.syscall_table = []
        self.syscall = []

        paths = str(pmem_paths)
        self.pmem_paths = paths.split(':')

        # counting PIDs
        self.pid_table = []
        self.npids = 0
        self.last_pid = -1
        self.last_pid_ind = 0

        self.all_strings = ["(stdin)", "(stdout)", "(stderr)"]
        self.all_fd_tables = []
        self.path_is_pmem = [0, 0, 0]

        self.list_unsup = []
        self.list_unsup_yet = []
        self.list_unsup_rel = []
        self.list_unsup_flag = []

        self.ind_unsup = []
        self.ind_unsup_yet = []
        self.ind_unsup_rel = []
        self.ind_unsup_flag = []

        if max_packets:
            self.max_packets = int(max_packets)
        else:
            self.max_packets = -1

        if fileout:
            self.fileout = fileout
            self.fhout = open_file(self.fileout, 'wt')
        else:
            if self.convert_mode or self.debug_mode:
                self.fileout = ""
                self.fhout = stdout
            else:
                if not script_mode:
                    print("Notice: output of analysis will be saved in the file: /tmp/antool-analysis-output")
                self.fileout = "/tmp/antool-analysis-output"
                self.fhout = open_file(self.fileout, 'wt')

        self.list_ok = ListSyscalls(script_mode, debug_mode, self.verbose_mode, self.fhout)
        self.list_no_exit = ListSyscalls(script_mode, debug_mode, self.verbose_mode, self.fhout)
        self.list_others = ListSyscalls(script_mode, debug_mode, self.verbose_mode, self.fhout)

    def read_syscall_table(self, path_to_syscalls_table_dat):
        self.syscall_table = SyscallTable()
        if self.syscall_table.read(path_to_syscalls_table_dat):
            print("Error while reading syscalls table", file=stderr)
            exit(-1)

    def print_log(self):
        self.list_ok.print()

        if self.debug_mode and len(self.list_no_exit):
            print("\nWarning: list 'list_no_exit' is not empty!")
            self.list_no_exit.sort()
            self.list_no_exit.print_always()

        if self.debug_mode and len(self.list_others):
            print("\nWarning: list 'list_others' is not empty!")
            self.list_others.sort()
            self.list_others.print_always()

    ###############################################################################
    # analyze_check - analyze check result
    ###############################################################################
    def analyze_check(self, check, info_all, pid_tid, sc_id, name, retval):

        if CHECK_IGNORE == check:
            return DO_CONTINUE

        elif CHECK_SKIP == check:
            if self.debug_mode:
                print("Warning: skipping wrong packet type {0:d} of {1:s} ({2:d})"
                      .format(info_all, self.syscall_table.name(sc_id), sc_id))
            return DO_CONTINUE

        elif CHECK_NO_EXIT == check:
            self.list_no_exit.append(self.syscall)
            return DO_REINIT

        elif check in (CHECK_NO_ENTRY, CHECK_SAVE_IN_ENTRY, CHECK_WRONG_EXIT):
            old_syscall = self.syscall
            if CHECK_SAVE_IN_ENTRY == check:
                self.list_others.append(self.syscall)
            if retval != 0 or name not in ("clone", "fork", "vfork"):
                self.syscall = self.list_no_exit.search(info_all, pid_tid, sc_id, name, retval)
            if CHECK_WRONG_EXIT == check:
                self.list_no_exit.append(old_syscall)
            if retval == 0 and name in ("clone", "fork", "vfork"):
                return DO_REINIT
            if self.debug_mode:
                if self.syscall == -1:
                    print("Warning: NO ENTRY found: exit without entry info found: {0:s} (sc_id:{1:d})"
                          .format(name, sc_id))
                else:
                    print("Notice: found matching ENTRY for: {0:s} (sc_id:{1:d} pid:{2:016X}):"
                          .format(name, sc_id, pid_tid))
            if self.syscall == -1:
                return DO_REINIT
            else:
                return DO_GO_ON

        elif CHECK_WRONG_ID == check:
            self.list_others.append(self.syscall)
            return DO_REINIT


    ###############################################################################
    # read_and_parse_data - read and parse data
    ###############################################################################
    def read_and_parse_data(self, path_to_trace_log):
        sizei = struct.calcsize('i')
        sizeI = struct.calcsize('I')
        sizeQ = struct.calcsize('Q')

        fh = open_file(path_to_trace_log, 'rb')

        # read and init global buf_size
        buf_size, = read_fmt_data(fh, 'i')

        # read length of CWD
        cwd_len, = read_fmt_data(fh, 'i')
        bdata = fh.read(cwd_len)
        cwd = str(bdata.decode(errors="ignore"))
        self.cwd = cwd.replace('\0', ' ')

        # read header = command line
        data_size, argc = read_fmt_data(fh, 'ii')
        data_size -= sizei
        bdata = fh.read(data_size)
        argv = str(bdata.decode(errors="ignore"))
        argv = argv.replace('\0', ' ')

        if not self.script_mode:
            # noinspection PyTypeChecker
            print("Current working directory:", self.cwd, file=self.fhout)
            # noinspection PyTypeChecker
            print("Command line:", argv, file=self.fhout)
            if not self.debug_mode:
                print("\nReading packets:")

        n = 0
        state = STATE_INIT
        while True:
            try:
                if not self.debug_mode and not self.script_mode:
                    print("\r{0:d}".format(n), end=' ')
                n += 1

                if state == STATE_COMPLETED:
                    if n > self.max_packets > 0:
                        if not self.script_mode:
                            print("done (read maximum number of packets: {0:d})".format(n - 1))
                        break
                    state = STATE_INIT

                data_size, info_all, pid_tid, sc_id, timestamp = read_fmt_data(fh, 'IIQQQ')
                data_size = data_size - (sizeI + 3 * sizeQ)

                # read the rest of data
                bdata = read_bdata(fh, data_size)

                if state == STATE_INIT:
                    self.syscall = Syscall(pid_tid, sc_id, self.syscall_table.get(sc_id), buf_size, self.debug_mode)

                name = self.syscall_table.name(sc_id)
                retval = self.syscall.get_ret(bdata)

                check = self.syscall.do_check(info_all, pid_tid, sc_id, name, retval)
                result = self.analyze_check(check, info_all, pid_tid, sc_id, name, retval)
                if result == DO_CONTINUE:
                    continue
                elif result == DO_REINIT:
                    self.syscall = Syscall(pid_tid, sc_id, self.syscall_table.get(sc_id), buf_size, self.debug_mode)

                state = self.syscall.add_data(info_all, bdata, timestamp)

                if state == STATE_COMPLETED:
                    if not self.offline_mode:
                        # XXX print("NAME:", self.syscall.name) # XXX
                        self.syscall.pid_ind = self.count_pids(self.syscall.pid_tid)
                        if self.no_entry_content(self.syscall):
                            continue
                        self.match_fd_with_path(self.syscall)
                        self.syscall.unsupported = self.check_if_supported(self.syscall)
                        self.add_to_unsupported_lists_or_print(self.syscall)
                    else:
                        self.list_ok.append(self.syscall)

                if self.debug_mode:
                    self.syscall.debug_print()

                if self.syscall.truncated:
                    truncated = self.syscall.truncated
                    print("Error: string argument number {0:d} is truncated!: {1:s}"
                          .format(truncated, self.syscall.args[truncated - 1]), file=stderr)
                    exit(-1)

            except EndOfFile as err:
                if err.val > 0:
                    print("Warning: log file is truncated:", path_to_trace_log, file=stderr)
                break
            except:
                print("Unexpected error:", exc_info()[0], file=stderr)
                raise

        fh.close()

        if not self.debug_mode and not self.script_mode:
            print("\rDone (read {0:d} packets).".format(n))

        if not self.offline_mode:
            self.print_unsupported_syscalls()
        else:
            if len(self.list_no_exit):
                self.list_ok += self.list_no_exit
            if len(self.list_others):
                self.list_ok += self.list_others
            self.list_ok.sort()
            # self.list_ok.make_time_relative()

    def count_pids_offline(self):
        self.list_ok.count_pids_offline()

    def match_fd_with_path_offline(self, pmem_paths):
        self.list_ok.match_fd_with_path_offline(self.cwd, pmem_paths)

    def print_unsupported_syscalls_offline(self):
        self.list_ok.print_unsupported_syscalls_offline()


###############################################################################
# main
###############################################################################

def main():
    parser = argparse.ArgumentParser(
                        description="Analyzing Tool - analyze binary logs of vltrace "
                                    "and check if all recorded syscalls are supported by pmemfile")

    parser.add_argument("-t", "--table", required=True,
                        help="path to the 'syscalls_table.dat' file generated by vltrace")
    parser.add_argument("-b", "--binlog", required=True, help="path to a vltrace log in binary format")

    parser.add_argument("-c", "--convert", action='store_true', required=False,
                        help="converter mode - only converts vltrace log from binary to text format")

    parser.add_argument("-p", "--pmem", required=False, help="paths to colon-separated pmem filesystems")
    parser.add_argument("-m", "--max_packets", required=False,
                        help="maximum number of packets to be read from the vltrace binary log")

    parser.add_argument("-l", "--log", action='store_true', required=False, help="print converted log in analyze mode")
    parser.add_argument("-o", "--output", required=False, help="file to save analysis output")

    parser.add_argument("-s", "--script", action='store_true', required=False,
                        help="script mode - print only the most important information (eg. no info about progress)")
    parser.add_argument("-v", "--verbose", action='count', required=False,
                        help="verbose mode (-v: verbose, -vv: very verbose)")
    parser.add_argument("-d", "--debug", action='store_true', required=False, help="debug mode")
    parser.add_argument("-f", "--offline", action='store_true', required=False, help="offline analysis mode")

    args = parser.parse_args()

    at = AnalyzingTool(args.convert, args.pmem, args.script, args.debug,
                        args.output, args.max_packets, args.verbose, args.offline)
    at.read_syscall_table(args.table)
    at.read_and_parse_data(args.binlog)

    if args.convert or args.log:
        at.print_log()

    if args.convert or not args.offline:
        return

    at.count_pids_offline()
    at.match_fd_with_path_offline(args.pmem)
    at.print_unsupported_syscalls_offline()


if __name__ == "__main__":
    main()
