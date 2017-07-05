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

import struct
import argparse

from syscalltable import *
from listsyscalls import *

DO_GO_ON = 0
DO_REINIT = 1
DO_SKIP = 2

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

        self.print_progress = not (self.debug_mode or self.script_mode or (self.convert_mode and not self.offline_mode)
                                    or (not self.convert_mode and self.verbose_mode >= 2))

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

        format = '%(levelname)s: %(message)s'

        if debug_mode:
            level=logging.DEBUG
        elif verbose_mode:
            level=logging.INFO
        else:
            level=logging.WARNING

        if fileout:
            logging.basicConfig(format=format, level=level, filename=fileout)
        else:
            logging.basicConfig(format=format, level=level)

        logging.debug("convert_mode   = {0:d}".format(self.convert_mode))
        logging.debug("script_mode    = {0:d}".format(self.script_mode))
        logging.debug("offline_mode   = {0:d}".format(self.offline_mode))
        logging.debug("verbose_mode   = {0:d}".format(self.verbose_mode))
        logging.debug("debug_mode     = {0:d}".format(self.debug_mode))
        logging.debug("print_progress = {0:d}".format(self.print_progress))

        self.list_ok = ListSyscalls(script_mode, debug_mode, self.verbose_mode)
        self.list_no_exit = ListSyscalls(script_mode, debug_mode, self.verbose_mode)
        self.list_others = ListSyscalls(script_mode, debug_mode, self.verbose_mode)

    def read_syscall_table(self, path_to_syscalls_table_dat):
        self.syscall_table = SyscallTable()
        if self.syscall_table.read(path_to_syscalls_table_dat):
            logging.error("error while reading syscalls table")
            exit(-1)

    def print_log(self):
        self.list_ok.print()

        if self.debug_mode and len(self.list_no_exit):
            print("\nWARNING: list 'list_no_exit' is not empty!")
            self.list_no_exit.sort()
            self.list_no_exit.print_always()

        if self.debug_mode and len(self.list_others):
            print("\nWARNING: list 'list_others' is not empty!")
            self.list_others.sort()
            self.list_others.print_always()

    ###############################################################################
    # analyze_check - analyze check result
    ###############################################################################
    def analyze_check(self, check, info_all, pid_tid, sc_id, name, retval):

        if CHECK_IGNORE == check:
            return DO_SKIP

        if CHECK_SKIP == check:
            if self.debug_mode:
                logging.debug("WARNING: skipping wrong packet type {0:d} of {1:s} ({2:d})"
                                .format(info_all, self.syscall_table.name(sc_id), sc_id))
            return DO_SKIP

        if CHECK_NO_EXIT == check:
            self.list_no_exit.append(self.syscall)
            return DO_REINIT

        if check in (CHECK_NO_ENTRY, CHECK_SAVE_IN_ENTRY, CHECK_WRONG_EXIT):
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
                    logging.debug("WARNING: no entry found: exit without entry info found: {0:s} (sc_id:{1:d})"
                                    .format(name, sc_id))
                else:
                    logging.debug("Notice: found matching entry for: {0:s} (sc_id:{1:d} pid:{2:016X}):"
                                    .format(name, sc_id, pid_tid))

            if self.syscall == -1:
                return DO_REINIT

            return DO_GO_ON

        if CHECK_WRONG_ID == check:
            self.list_others.append(self.syscall)
            return DO_REINIT

        return DO_GO_ON


    ###############################################################################
    # do_analyse - do syscall analysis
    ###############################################################################
    def do_analyse(self, syscall):
        syscall.pid_ind = self.count_pids(syscall.pid_tid)
        if self.has_entry_content(syscall):
            self.match_fd_with_path(syscall)
            syscall.unsupported = self.check_if_supported(syscall)
            self.add_to_unsupported_lists_or_print(syscall)
        return syscall


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
            logging.info("Command line: {0:s}".format(argv))
            # noinspection PyTypeChecker
            logging.info("Current working directory: {0:s}".format(self.cwd))
            if self.print_progress:
                print("Reading packets:")

        n = 0
        state = STATE_INIT
        while True:
            try:
                data_size, info_all, pid_tid, sc_id, timestamp = read_fmt_data(fh, 'IIQQQ')
                data_size = data_size - (sizeI + 3 * sizeQ)
                bdata = read_bdata(fh, data_size)

                n += 1
                if self.print_progress:
                    print("\r{0:d}".format(n), end=' ')
                if n >= self.max_packets > 0:
                    if not self.script_mode:
                        print("done (read maximum number of packets: {0:d})".format(n))
                    break

                if state == STATE_COMPLETED:
                    state = STATE_INIT

                if state == STATE_INIT:
                    self.syscall = Syscall(pid_tid, sc_id, self.syscall_table.get(sc_id), buf_size, self.debug_mode)

                name = self.syscall_table.name(sc_id)
                retval = self.syscall.get_ret(bdata)

                check = self.syscall.do_check(info_all, pid_tid, sc_id, name, retval)
                result = self.analyze_check(check, info_all, pid_tid, sc_id, name, retval)
                if result == DO_SKIP:
                    continue
                if result == DO_REINIT:
                    self.syscall = Syscall(pid_tid, sc_id, self.syscall_table.get(sc_id), buf_size, self.debug_mode)

                state = self.syscall.add_data(info_all, bdata, timestamp)

                if state == STATE_COMPLETED:
                    if self.offline_mode:
                        self.list_ok.append(self.syscall)
                    elif not self.convert_mode:
                        self.syscall = self.do_analyse(self.syscall)

                if (self.convert_mode and not self.offline_mode) or self.debug_mode:
                    self.syscall.print_single_record()

                if self.syscall.truncated:
                    truncated = self.syscall.truncated
                    logging.error("string argument number {0:d} is truncated: {1:s}"
                                    .format(truncated, self.syscall.args[truncated - 1]))

            except EndOfFile as err:
                if err.val > 0:
                    logging.error("log file is truncated: {0:s}".format(path_to_trace_log))
                break

            except:
                logging.critical("unexpected error")
                raise

        fh.close()

        if self.print_progress:
            print("\rDone (read {0:d} packets).".format(n))

        if len(self.list_no_exit):
            self.list_ok += self.list_no_exit
        if len(self.list_others):
            self.list_ok += self.list_others
        self.list_ok.sort()

        if not self.convert_mode and not self.offline_mode:
            for n in range(len(self.list_ok)):
                self.list_ok[n] = self.do_analyse(self.list_ok[n])
            self.print_unsupported_syscalls()

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

    if args.offline and (args.convert or args.log):
        at.print_log()

    if args.convert or not args.offline:
        return

    at.count_pids_offline()
    at.match_fd_with_path_offline(args.pmem)
    at.print_unsupported_syscalls_offline()


if __name__ == "__main__":
    main()
