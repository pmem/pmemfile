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

from listsyscalls import *
from converter import *


########################################################################################################################
# Analyzer
########################################################################################################################
class Analyzer(Converter):
    def __init__(self, convert_mode, pmem_paths, slink_file, fileout, max_packets, offline_mode,
                 script_mode, debug_mode, print_log_mode, verbose_mode, print_all):

        Converter.__init__(self, fileout, max_packets, offline_mode, script_mode, debug_mode, verbose_mode)

        self.convert_mode = convert_mode
        self.script_mode = script_mode
        self.debug_mode = debug_mode
        self.offline_mode = offline_mode
        self.print_log_mode = print_log_mode
        self.verbose_mode = verbose_mode

        self.print_progress = not (self.debug_mode or self.script_mode or self.print_log_mode
                                   or (self.convert_mode and not self.offline_mode)
                                   or (not self.convert_mode and self.verbose_mode >= 2))

        self.print_single_record = not self.offline_mode and (self.convert_mode or self.print_log_mode)

        self.syscall_table = SyscallTable()
        self.syscall = Syscall(0, 0, SyscallInfo("", 0, 0, 0), 0, 0)
        self.buf_size = 0

        if max_packets:
            self.max_packets = int(max_packets)
        else:
            self.max_packets = -1

        log_format = '%(levelname)s(%(name)s): %(message)s'

        if debug_mode:
            level = logging.DEBUG
        elif verbose_mode:
            level = logging.INFO
        else:
            level = logging.WARNING

        if fileout:
            logging.basicConfig(format=log_format, level=level, filename=fileout)
        else:
            logging.basicConfig(format=log_format, level=level)

        self.log_main = logging.getLogger("main")

        self.log_main.debug("convert_mode   = {0:d}".format(self.convert_mode))
        self.log_main.debug("script_mode    = {0:d}".format(self.script_mode))
        self.log_main.debug("offline_mode   = {0:d}".format(self.offline_mode))
        self.log_main.debug("verbose_mode   = {0:d}".format(self.verbose_mode))
        self.log_main.debug("debug_mode     = {0:d}".format(self.debug_mode))
        self.log_main.debug("print_progress = {0:d}".format(self.print_progress))

        self.list_ok = ListSyscalls(pmem_paths, slink_file, script_mode, debug_mode, verbose_mode, print_all,
                                    init_pmem=1)
        self.list_no_exit = ListSyscalls(pmem_paths, slink_file, script_mode, debug_mode, verbose_mode, print_all)
        self.list_no_entry = ListSyscalls(pmem_paths, slink_file, script_mode, debug_mode, verbose_mode, print_all)
        self.list_others = ListSyscalls(pmem_paths, slink_file, script_mode, debug_mode, verbose_mode, print_all)

    ####################################################################################################################
    def process_complete_syscall(self, syscall):
        assert(syscall.is_complete())

        if self.offline_mode:
            self.list_ok.append(syscall)
        elif not self.convert_mode:
            syscall = self.list_ok.analyse_if_supported_syscall(syscall)

        return syscall

    ####################################################################################################################
    def link_lists(self):
        if len(self.list_no_entry):
            self.list_ok += self.list_no_entry
        if len(self.list_no_exit):
            self.list_ok += self.list_no_exit
        if len(self.list_others):
            self.list_ok += self.list_others
        self.list_ok.sort()

    ####################################################################################################################
    def analyse_and_print_unsupported_online(self):
        assert(not self.convert_mode and not self.offline_mode)

        self.list_ok.analyse_if_supported_list()
        self.list_ok.print_unsupported_syscalls()

    ####################################################################################################################
    def set_pid_index_offline(self):
        self.list_ok.set_pid_index_offline()

    ####################################################################################################################
    def match_fd_with_path_offline(self):
        self.list_ok.match_fd_with_path_offline()

    ####################################################################################################################
    def print_unsupported_syscalls_offline(self):
        self.list_ok.print_unsupported_syscalls_offline()

    ####################################################################################################################
    def process_cwd(self, cwd):
        self.list_ok.set_first_cwd(cwd)
