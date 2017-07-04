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

from sys import stdout
from syscall import *

EM_str_1 = 1 << 0  # syscall has string as 1. argument
EM_str_2 = 1 << 1  # syscall has string as 2. argument
EM_str_3 = 1 << 2  # syscall has string as 3. argument
EM_str_4 = 1 << 3  # syscall has string as 4. argument
EM_str_5 = 1 << 4  # syscall has string as 5. argument
EM_str_6 = 1 << 5  # syscall has string as 6. argument

EM_fd_1 = 1 << 6  # syscall has fd as a 1. arg
EM_fd_2 = 1 << 7  # syscall has fd as a 2. arg
EM_fd_3 = 1 << 8  # syscall has fd as a 3. arg
EM_fd_4 = 1 << 9  # syscall has fd as a 4. arg
EM_fd_5 = 1 << 10  # syscall has fd as a 5. arg
EM_fd_6 = 1 << 11  # syscall has fd as a 6. arg

EM_path_1 = 1 << 12  # syscall has path as 1. arg
EM_path_2 = 1 << 13  # syscall has path as 2. arg
EM_path_3 = 1 << 14  # syscall has path as 3. arg
EM_path_4 = 1 << 15  # syscall has path as 4. arg
EM_path_5 = 1 << 16  # syscall has path as 5. arg
EM_path_6 = 1 << 17  # syscall has path as 6. arg

EM_fd_from_path = EM_rfd | EM_path_1
EM_fd_from_fd = EM_rfd | EM_fd_1
EM_fd_from_dirfd_path = EM_rfd | EM_fd_1 | EM_path_2

EM_isfileat = EM_fd_1 | EM_path_2 | EM_fileat
EM_isfileat2 = EM_fd_3 | EM_path_4 | EM_fileat2

EM_str_all = EM_str_1 | EM_str_2 | EM_str_3 | EM_str_4 | EM_str_5 | EM_str_6
EM_path_all = EM_path_1 | EM_path_2 | EM_path_3 | EM_path_4 | EM_path_5 | EM_path_6
EM_fd_all = EM_fd_1 | EM_fd_2 | EM_fd_3 | EM_fd_4 | EM_fd_5 | EM_fd_6

Arg_is_str = [EM_str_1, EM_str_2, EM_str_3, EM_str_4, EM_str_5, EM_str_6]
Arg_is_path = [EM_path_1, EM_path_2, EM_path_3, EM_path_4, EM_path_5, EM_path_6]
Arg_is_fd = [EM_fd_1, EM_fd_2, EM_fd_3, EM_fd_4, EM_fd_5, EM_fd_6]

FLAG_RENAME_WHITEOUT = (1 << 2)  # renameat2's flag: whiteout source
FLAG_O_ASYNC = 0o20000  # open's flag

# fallocate flags:
F_FALLOC_FL_COLLAPSE_RANGE = 0x08
F_FALLOC_FL_ZERO_RANGE = 0x10
F_FALLOC_FL_INSERT_RANGE = 0x20

# fcntl's flags:
F_SETFD = 2
F_GETLK = 5
F_SETLK = 6
F_SETLKW = 7
F_SETOWN = 8
F_GETOWN = 9
F_SETSIG = 10
F_GETSIG = 11
F_SETOWN_EX = 15
F_GETOWN_EX = 16
F_OFD_GETLK = 36
F_OFD_SETLK = 37
F_OFD_SETLKW = 38
F_SETLEASE = 1024
F_GETLEASE = 1025
F_NOTIFY = 1026
F_ADD_SEALS = 1033
F_GET_SEALS = 1034

FD_CLOEXEC = 1
AT_EMPTY_PATH = 0x1000

# clone() flags set by pthread_create():
# = CLONE_VM|CLONE_FS|CLONE_FILES|CLONE_SIGHAND|CLONE_THREAD|CLONE_SYSVSEM|CLONE_SETTLS|
#   CLONE_PARENT_SETTID|CLONE_CHILD_CLEARTID
F_PTHREAD_CREATE = 0x3d0f00


class ListSyscalls(list):
    def __init__(self, script_mode, debug_mode, verbose_mode, fhout):
        list.__init__(self)

        self.script_mode = script_mode
        self.debug_mode = debug_mode
        self.verbose_mode = verbose_mode
        self.fhout = fhout

        self.cwd = ""
        self.time0 = 0

        # counting PIDs
        self.pid_table = []
        self.npids = 0
        self.last_pid = -1
        self.last_pid_ind = 0

        self.all_strings = ["(stdin)", "(stdout)", "(stderr)"]
        self.all_fd_tables = []

        self.pmem_paths = str("")
        self.path_is_pmem = [0, 0, 0]

        self.unsupported = 0
        self.unsupported_yet = 0
        self.unsupported_rel = 0
        self.unsupported_flag = 0

        self.list_unsup = []
        self.list_unsup_yet = []
        self.list_unsup_rel = []
        self.list_unsup_flag = []

        self.ind_unsup = []
        self.ind_unsup_yet = []
        self.ind_unsup_rel = []
        self.ind_unsup_flag = []

    def check_if_path_is_pmem(self, string):
        string = str(string)
        for n in range(len(self.pmem_paths)):
            if string.find(self.pmem_paths[n]) == 0:
                return 1
        return 0

    def all_strings_append(self, string, is_pmem):
        if self.all_strings.count(string) == 0:
            self.all_strings.append(string)
            self.path_is_pmem.append(is_pmem)
            str_ind = len(self.all_strings) - 1
            # XXX print("all_strings_append =", str_ind, string, is_pmem)  # XXX
        else:
            str_ind = self.all_strings.index(string)
        return str_ind

    @staticmethod
    def fd_table_assign(table, fd, val):
        for i in range(len(table), fd + 1):
            table.append(-1)
        table[fd] = val

    def get_rel_time(self, timestamp):
        if self.time0:
            return timestamp - self.time0
        else:
            self.time0 = timestamp
            return 0

    def make_time_relative(self):
        for n in range(len(self)):
            self[n].time_start = self.get_rel_time(self[n].time_start)
            self[n].time_end = self.get_rel_time(self[n].time_end)

    def print(self):
        for n in range(len(self)):
            self[n].print()

    def print_always(self):
        for n in range(len(self)):
            self[n].print_always()

    def search(self, info_all, pid_tid, sc_id, name, retval):
        for n in range(len(self)):
            syscall = self[n]
            check = syscall.do_check(info_all, pid_tid, sc_id, name, retval)
            if check == CHECK_OK:
                del self[n]
                return syscall
        return -1

    def count_pids(self, pid_tid):
        pid = pid_tid >> 32
        if pid != self.last_pid:
            self.last_pid = pid
            if self.pid_table.count(pid) == 0:
                self.pid_table.append(pid)
                self.all_fd_tables.append([0, 1, 2])
                self.npids = len(self.pid_table)
                self.last_pid_ind = len(self.pid_table) - 1
            else:
                self.last_pid_ind = self.pid_table.index(pid)
        return self.last_pid_ind

    def count_pids_offline(self):
        length = len(self)
        if not self.script_mode:
            print("\nCounting PIDs:")
        for n in range(length):
            if not self.script_mode:
                print("\r{0:d} of {1:d} ({2:d}%)".format(n + 1, length, int((100 * (n + 1)) / length)), end='')
            self[n].pid_ind = self.count_pids(self[n].pid_tid)
        if not self.script_mode:
            print(" done.")
        if self.debug_mode:
            for n in range(len(self.pid_table)):
                print("PID[{0:d}] = {1:016X}".format(n, self.pid_table[n]), file=self.fhout)

    def arg_is_pmem(self, syscall, narg):
        if narg > syscall.sc.nargs:
            return 0
        narg -= 1
        if syscall.has_mask(Arg_is_path[narg] | Arg_is_fd[narg]):
            str_ind = syscall.args[narg]
            if str_ind != -1 and str_ind < len(self.path_is_pmem) and self.path_is_pmem[str_ind]:
                return 1
        return 0

    def check_fallocate_flags(self, syscall):
        syscall.unsupported_flag = ""
        if syscall.args[1] == F_FALLOC_FL_COLLAPSE_RANGE:
            syscall.unsupported_flag = "FALLOC_FL_COLLAPSE_RANGE"
        elif syscall.args[1] == F_FALLOC_FL_ZERO_RANGE:
            syscall.unsupported_flag = "FALLOC_FL_ZERO_RANGE"
        elif syscall.args[1] == F_FALLOC_FL_INSERT_RANGE:
            syscall.unsupported_flag = "FALLOC_FL_INSERT_RANGE"
        if syscall.unsupported_flag != "":
            return RESULT_UNSUPPORTED_FLAG
        else:
            return RESULT_SUPPORTED

    def check_fcntl_flags(self, syscall):
        syscall.unsupported_flag = ""
        if syscall.args[1] == F_SETFD and (syscall.args[2] & FD_CLOEXEC == 0):
            syscall.unsupported_flag = "F_SETFD: not possible to clear FD_CLOEXEC flag"
        elif syscall.args[1] == F_GETLK:
            syscall.unsupported_flag = "F_GETLK"
        elif syscall.args[1] == F_SETLK:
            syscall.unsupported_flag = "F_SETLK"
        elif syscall.args[1] == F_SETLKW:
            syscall.unsupported_flag = "F_SETLKW"
        elif syscall.args[1] == F_SETOWN:
            syscall.unsupported_flag = "F_SETOWN"
        elif syscall.args[1] == F_GETOWN:
            syscall.unsupported_flag = "F_GETOWN"
        elif syscall.args[1] == F_SETSIG:
            syscall.unsupported_flag = "F_SETSIG"
        elif syscall.args[1] == F_GETSIG:
            syscall.unsupported_flag = "F_GETSIG"
        elif syscall.args[1] == F_SETOWN_EX:
            syscall.unsupported_flag = "F_SETOWN_EX"
        elif syscall.args[1] == F_GETOWN_EX:
            syscall.unsupported_flag = "F_GETOWN_EX"
        elif syscall.args[1] == F_OFD_GETLK:
            syscall.unsupported_flag = "F_OFD_GETLK"
        elif syscall.args[1] == F_OFD_SETLK:
            syscall.unsupported_flag = "F_OFD_SETLK"
        elif syscall.args[1] == F_OFD_SETLKW:
            syscall.unsupported_flag = "F_OFD_SETLKW"
        elif syscall.args[1] == F_SETLEASE:
            syscall.unsupported_flag = "F_SETLEASE"
        elif syscall.args[1] == F_GETLEASE:
            syscall.unsupported_flag = "F_GETLEASE"
        elif syscall.args[1] == F_NOTIFY:
            syscall.unsupported_flag = "F_NOTIFY"
        elif syscall.args[1] == F_ADD_SEALS:
            syscall.unsupported_flag = "F_ADD_SEALS"
        elif syscall.args[1] == F_GET_SEALS:
            syscall.unsupported_flag = "F_GET_SEALS"
        if syscall.unsupported_flag != "":
            return RESULT_UNSUPPORTED_FLAG
        else:
            return RESULT_SUPPORTED

    def check_if_supported(self, syscall):
        if syscall.name in ("fork", "vfork"):
            return RESULT_UNSUPPORTED

        if syscall.name == "clone" and syscall.args[0] != F_PTHREAD_CREATE:
            syscall.unsupported_flag = "flags other than set by pthread_create()"
            return RESULT_UNSUPPORTED_FLAG

        if len(syscall.strings) > 0:
            if (len(syscall.strings[0]) > 0 and syscall.strings[0][0] != '/') or syscall.strings[0] == "":
                if syscall.name in ("chroot", "getxattr", "lgetxattr", "setxattr", "lsetxattr"):
                    return RESULT_UNSUPPORTED_RELATIVE

        if not syscall.is_pmem:
            return RESULT_SUPPORTED

        if syscall.has_mask(EM_rfd):  # open & openat - O_ASYNC
            if (syscall.is_mask(Arg_is_path[0]) and syscall.args[1] & FLAG_O_ASYNC and syscall.name == "open") or \
               (syscall.is_mask(Arg_is_fd[0] | Arg_is_path[1] | EM_fileat) and syscall.args[2] & FLAG_O_ASYNC and
               syscall.name == "openat"):
                syscall.unsupported_flag = "O_ASYNC"
                return RESULT_UNSUPPORTED_FLAG
            return RESULT_SUPPORTED

        if syscall.is_mask(EM_isfileat):
            if syscall.name in ("execveat", "name_to_handle_at"):
                return RESULT_UNSUPPORTED
            elif syscall.name in ("futimesat", "utimensat"):
                return RESULT_UNSUPPORTED_YET
            # renameat2 - RENAME_WHITEOUT
            elif syscall.sc.nargs == 5 and syscall.name in "renameat2" and syscall.args[4] & FLAG_RENAME_WHITEOUT:
                syscall.unsupported_flag = "RENAME_WHITEOUT"
                return RESULT_UNSUPPORTED_FLAG
            return RESULT_SUPPORTED

        # fallocate - FALLOC_FL_COLLAPSE_RANGE or FALLOC_FL_ZERO_RANGE or FALLOC_FL_INSERT_RANGE
        if syscall.has_mask(EM_fd_1):
            if syscall.name == "fallocate" and syscall.args[1]:
                return self.check_fallocate_flags(syscall)
            if syscall.name == "fcntl":
                return self.check_fcntl_flags(syscall)

        if self.arg_is_pmem(syscall, 1):
            if syscall.name in (
                    "chroot", "execve", "readahead",
                    "setxattr", "lsetxattr", "fsetxattr",
                    "listxattr", "llistxattr", "flistxattr",
                    "removexattr", "lremovexattr", "fremovexattr"):
                return RESULT_UNSUPPORTED
            elif syscall.name in (
                    "dup", "dup2", "dup3", "utime", "utimes", "flock"):
                return RESULT_UNSUPPORTED_YET

        if (self.arg_is_pmem(syscall, 1) or self.arg_is_pmem(syscall, 3)) and syscall.name in ("copy_file_range", "splice"):
            return RESULT_UNSUPPORTED_YET

        if (self.arg_is_pmem(syscall, 1) or self.arg_is_pmem(syscall, 2)) and syscall.name in ("sendfile", "sendfile64"):
            return RESULT_UNSUPPORTED_YET

        if self.arg_is_pmem(syscall, 5) and syscall.name == "mmap":
            return RESULT_UNSUPPORTED_YET

        return RESULT_SUPPORTED

    def handle_fileat(self, syscall, arg1, arg2, fhout):
        dirfd = syscall.args[arg1]
        if dirfd == 0xFFFFFFFFFFFFFF9C:  # AT_FDCWD
            dirfd = -100
        path = syscall.strings[syscall.args[arg2]]
        fd_out = syscall.iret

        # check if AT_EMPTY_PATH is set
        if syscall.sc.nargs > (arg2 + 1) and syscall.has_mask(Arg_is_path[arg2 + 1] | Arg_is_fd[arg2 + 1]) == 0 and\
           syscall.args[arg2 + 1] & AT_EMPTY_PATH:
            path = ""

        dir_str = ""
        newpath = path
        unknown_dirfd = 0
        if (len(path) == 0 and not syscall.read_error) or (len(path) != 0 and path[0] != '/'):
            fd_table = self.all_fd_tables[syscall.pid_ind]
            if dirfd == -100:
                dir_str = self.cwd
                newpath = dir_str + "/" + path
            elif 0 <= dirfd < len(fd_table):
                str_ind = fd_table[dirfd]
                syscall.args[arg1] = str_ind
                dir_str = self.all_strings[str_ind]
                newpath = dir_str + "/" + path
            elif syscall.has_mask(EM_rfd) and fd_out != -1:
                unknown_dirfd = 1

        if newpath != path:
            print(" {0:s} {1:s}".format(dir_str, path), end='', file=fhout)
            path = newpath
        else:
            print(" ({0:d}) {1:s}".format(dirfd, path), end='', file=fhout)

        is_pmem = self.check_if_path_is_pmem(path)
        str_ind = self.all_strings_append(path, is_pmem)
        syscall.args[arg2] = str_ind
        syscall.is_pmem |= is_pmem
        if is_pmem:
            print(" [PMEM]", end='', file=fhout)
        if unknown_dirfd:
            print("Error: unknown dirfd :", dirfd, file=fhout)
        return path, is_pmem, fd_out

    def match_fd_with_path(self, syscall):
        if syscall.read_error:
            if not self.script_mode:
                print(file=stderr)
            print("Warning: BPF read error occurred, path is empty in syscall:", syscall.name, file=stderr)
            print("Warning: BPF read error occurred, path is empty in syscall:", syscall.name, file=self.fhout)

        # syscalls: SyS_open or SyS_creat
        if syscall.is_mask(EM_fd_from_path):
            path = syscall.strings[0]
            if (len(path) == 0 or path[0] != '/') and not syscall.read_error:
                path = self.cwd + "/" + path
            is_pmem = self.check_if_path_is_pmem(path)
            syscall.is_pmem = is_pmem
            str_ind = self.all_strings_append(path, is_pmem)
            syscall.args[0] = str_ind
            if is_pmem:
                print("{0:20s} {1:s} [PMEM]".format(syscall.name, path), file=self.fhout)
            else:
                print("{0:20s} {1:s}".format(syscall.name, path), file=self.fhout)
            fd_out = syscall.iret
            if fd_out != -1:
                fd_table = self.all_fd_tables[syscall.pid_ind]
                self.fd_table_assign(fd_table, fd_out, str_ind)

        # all *at syscalls
        elif syscall.is_mask(EM_isfileat):
            print("{0:20s}".format(syscall.name), end='', file=self.fhout)
            path, is_pmem, fd_out = self.handle_fileat(syscall, 0, 1, self.fhout)
            # syscall SyS_openat
            if syscall.has_mask(EM_rfd) and fd_out != -1:
                str_ind = self.all_strings_append(path, is_pmem)
                fd_table = self.all_fd_tables[syscall.pid_ind]
                self.fd_table_assign(fd_table, fd_out, str_ind)
            if syscall.is_mask(EM_isfileat2):
                self.handle_fileat(syscall, 2, 3, self.fhout)
            print(file=self.fhout)

        # syscalls: SyS_dup*
        elif syscall.is_mask(EM_fd_from_fd):
            fd_table = self.all_fd_tables[syscall.pid_ind]
            fd_in = syscall.args[0]
            fd_out = syscall.iret
            if 0 <= fd_in < len(fd_table):
                str_ind = fd_table[fd_in]
                syscall.args[0] = str_ind
                path = self.all_strings[str_ind]
                if self.path_is_pmem[str_ind]:
                    syscall.is_pmem = 1
                    print("{0:20s} {1:s} [PMEM]".format(syscall.name, path), file=self.fhout)
                else:
                    print("{0:20s} {1:s}".format(syscall.name, path), file=self.fhout)
                if fd_out != -1:
                    self.fd_table_assign(fd_table, fd_out, str_ind)
            else:
                syscall.args[0] = -1
                print("{0:20s} ({1:d})".format(syscall.name, fd_in), file=self.fhout)
                if fd_out != -1:
                    print("Error: unknown fd :", fd_in, file=self.fhout)

        # close ()
        elif syscall.name == "close":
            fd_in = syscall.args[0]
            fd_table = self.all_fd_tables[syscall.pid_ind]
            if 0 <= fd_in < len(fd_table):
                str_ind = fd_table[fd_in]
                fd_table[fd_in] = -1
                path = self.all_strings[str_ind]
                if self.path_is_pmem[str_ind]:
                    syscall.is_pmem = 1
                    print("{0:20s} {1:s} [PMEM]".format(syscall.name, path), file=self.fhout)
                else:
                    print("{0:20s} {1:s}".format(syscall.name, path), file=self.fhout)
            else:
                print("{0:20s} (0x{1:016X})".format(syscall.name, fd_in), file=self.fhout)

        # syscalls with path or file descriptor
        elif syscall.has_mask(EM_str_all | EM_fd_all):
            print("{0:20s}".format(syscall.name), end='', file=self.fhout)
            for narg in range(syscall.sc.nargs):
                if syscall.has_mask(Arg_is_str[narg]):
                    is_pmem = 0
                    path = syscall.strings[syscall.args[narg]]
                    if syscall.has_mask(Arg_is_path[narg]):
                        syscall.str_is_path.append(1)
                        if len(path) != 0 and path[0] != '/':
                            self.all_strings_append(path, 0)  # add relative path as non-pmem
                            path = self.cwd + "/" + path
                        elif len(path) == 0 and not syscall.read_error:
                            path = self.cwd
                        is_pmem = self.check_if_path_is_pmem(path)
                    else:
                        syscall.str_is_path.append(0)
                    syscall.is_pmem |= is_pmem
                    str_ind = self.all_strings_append(path, is_pmem)
                    syscall.args[narg] = str_ind
                    if is_pmem:
                        print(" {0:s} [PMEM]".format(path), end='', file=self.fhout)
                    else:
                        print(" {0:s}".format(path), end='', file=self.fhout)
                if syscall.has_mask(Arg_is_fd[narg]):
                    fd_table = self.all_fd_tables[syscall.pid_ind]
                    fd = syscall.args[narg]
                    if fd in (0xFFFFFFFF, 0xFFFFFFFFFFFFFFFF):
                        fd = -1
                    if 0 <= fd < len(fd_table):
                        str_ind = fd_table[fd]
                        path = self.all_strings[str_ind]
                        if self.path_is_pmem[str_ind]:
                            syscall.is_pmem = 1
                            print(" {0:s} [PMEM]".format(path), end='', file=self.fhout)
                        else:
                            print(" {0:s}".format(path), end='', file=self.fhout)
                        syscall.args[narg] = str_ind
                    else:
                        if fd < 1024:
                            print(" ({0:d})".format(fd), end='', file=self.fhout)
                        else:
                            print(" (0x{0:016X})".format(fd), end='', file=self.fhout)
                        syscall.args[narg] = -1
            print(file=self.fhout)

    def has_entry_content(self, syscall):
        if not (syscall.content & CNT_ENTRY):  # no entry info (no info about arguments)
            if syscall.name not in ("clone", "fork", "vfork"):
                if not self.script_mode:
                    print()
                print("Warning: missing info about arguments of syscall: {0:s} - skipping..."
                      .format(syscall.name), file=stderr)
            return 0
        return 1

    def match_fd_with_path_offline(self, cwd, pmem_paths):
        self.cwd = cwd
        paths = str(pmem_paths)
        self.pmem_paths = paths.split(':')

        length = len(self)
        if not self.script_mode:
            print("\nAnalyzing:")

        for n in range(length):
            if self.fhout != stdout and not self.script_mode:
                print("\r{0:d} of {1:d} ({2:d}%)".format(n + 1, length, int((100 * (n + 1)) / length)), end='')
            if not self.has_entry_content(self[n]):
                continue
            self.match_fd_with_path(self[n])
            self[n].unsupported = self.check_if_supported(self[n])

        if self.fhout != stdout and not self.script_mode:
            print(" done.\n")

    def print_syscall(self, syscall, relative, end):
        # print("\t{0:16s}\t".format(syscall.name), end='')
        print("   {0:20s}\t\t".format(syscall.name), end='')
        if relative:
            for nstr in range(len(syscall.strings)):
                print(" {0:s}".format(syscall.strings[nstr]), end='')
        else:
            for narg in range(syscall.sc.nargs):
                if syscall.has_mask(Arg_is_str[narg] | Arg_is_fd[narg]):
                    str_ind = syscall.args[narg]
                    if str_ind != -1:
                        if self.path_is_pmem[str_ind]:
                            print(" {0:s} [PMEM]  ".format(self.all_strings[str_ind]), end='')
                        else:
                            print(" {0:s}".format(self.all_strings[str_ind]), end='')
        if end:
            print()

    def print_unsupported(self, l_names, l_inds):
        len_names = len(l_names)
        for n in range(len_names):
            if not self.verbose_mode:
                print("   {0:s}".format(l_names[n]))
            else:
                list_ind = l_inds[n]
                len_inds = len(list_ind)
                if len_inds:
                    print("   {0:s}:".format(l_names[n]))
                else:
                    print("   {0:s}".format(l_names[n]))
                for i in range(len_inds):
                    if self.path_is_pmem[list_ind[i]]:
                        print("\t\t{0:s} [PMEM]".format(self.all_strings[list_ind[i]]))
                    else:
                        print("\t\t{0:s}".format(self.all_strings[list_ind[i]]))

    def print_unsupported_verbose2(self, msg, syscall, relative, end):
        print("{0:28s}\t{1:16s}\t".format(msg, syscall.name), end='')
        if relative:
            for nstr in range(len(syscall.strings)):
                print(" {0:s}".format(syscall.strings[nstr]), end='')
        else:
            for narg in range(syscall.sc.nargs):
                if syscall.has_mask(Arg_is_path[narg] | Arg_is_fd[narg]):
                    str_ind = syscall.args[narg]
                    if str_ind != -1:
                        if self.path_is_pmem[str_ind]:
                            print(" {0:s} [PMEM]  ".format(self.all_strings[str_ind]), end='')
                        else:
                            print(" {0:s}".format(self.all_strings[str_ind]), end='')
        if end:
            print()

    def add_to_unsupported_lists(self, syscall, name, l_names, l_inds, relative):
        if l_names.count(name) == 0:
            l_names.append(name)
            ind = len(l_names) - 1
            list_ind = []
            l_inds.append(list_ind)
            assert (len(l_inds) - 1 == ind)
        else:
            ind = l_names.index(name)
            list_ind = l_inds[ind]

        if relative:
            for nstr in range(len(syscall.strings)):
                if syscall.str_is_path[nstr]:
                    string = syscall.strings[nstr]
                    assert (self.all_strings.count(string))
                    str_ind = self.all_strings.index(string)
                    if list_ind.count(str_ind) == 0:
                        list_ind.append(str_ind)
        else:
            for narg in range(syscall.sc.nargs):
                if syscall.has_mask(Arg_is_path[narg] | Arg_is_fd[narg]):
                    str_ind = syscall.args[narg]
                    if str_ind != -1:
                        if list_ind.count(str_ind) == 0:
                            list_ind.append(str_ind)
        l_inds[ind] = list_ind

    def add_to_unsupported_lists_or_print(self, syscall):
        if syscall.unsupported == RESULT_UNSUPPORTED:
            if self.verbose_mode >= 2:
                self.print_unsupported_verbose2("unsupported syscall:", syscall, relative=0, end=1)
            else:
                self.add_to_unsupported_lists(syscall, syscall.name, self.list_unsup, self.ind_unsup, relative=0)

        elif syscall.unsupported == RESULT_UNSUPPORTED_FLAG:
            if self.verbose_mode >= 2:
                self.print_unsupported_verbose2("unsupported flag:", syscall, relative=0, end=0)
                print(" [unsupported flag:]", syscall.unsupported_flag)
            else:
                name = syscall.name + " <" + syscall.unsupported_flag + ">"
                self.add_to_unsupported_lists(syscall, name, self.list_unsup_flag, self.ind_unsup_flag, relative=0)

        elif syscall.unsupported == RESULT_UNSUPPORTED_RELATIVE:
            if self.verbose_mode >= 2:
                self.print_unsupported_verbose2("unsupported relative path:", syscall, relative=1, end=1)
            else:
                self.add_to_unsupported_lists(syscall, syscall.name, self.list_unsup_rel, self.ind_unsup_rel, relative=1)

        elif syscall.unsupported == RESULT_UNSUPPORTED_YET:
            if self.verbose_mode >= 2:
                self.print_unsupported_verbose2("unsupported syscall yet:", syscall, relative=0, end=1)
            else:
                self.add_to_unsupported_lists(syscall, syscall.name, self.list_unsup_yet, self.ind_unsup_yet, relative=0)

    def print_unsupported_syscalls(self):
        if self.verbose_mode >= 2:
            return

        # RESULT_UNSUPPORTED
        if len(self.list_unsup):
            print("Unsupported syscalls detected:")
            self.print_unsupported(self.list_unsup, self.ind_unsup)
            print()

        # RESULT_UNSUPPORTED_FLAG
        if len(self.list_unsup_flag):
            print("Unsupported syscall's flag detected:")
            self.print_unsupported(self.list_unsup_flag, self.ind_unsup_flag)
            print()

        # RESULT_UNSUPPORTED_RELATIVE
        if len(self.list_unsup_rel):
            print("Unsupported syscalls with relative path detected:")
            self.print_unsupported(self.list_unsup_rel, self.ind_unsup_rel)
            print()

        # RESULT_UNSUPPORTED_YET
        if len(self.list_unsup_yet):
            print("Yet-unsupported syscalls detected (will be supported):")
            self.print_unsupported(self.list_unsup_yet, self.ind_unsup_yet)
            print()

        if not (len(self.list_unsup) or len(self.list_unsup_flag) or len(self.list_unsup_rel) or len(self.list_unsup_yet)):
            print("All syscalls are supported.")

    def print_unsupported_syscalls_offline(self):
        for n in range(len(self)):
            self.add_to_unsupported_lists_or_print(self[n])
        self.print_unsupported_syscalls()
