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

from syscall import *
from utils import *

RESULT_UNSUPPORTED_YET = 1
RESULT_UNSUPPORTED_FLAG = 2
RESULT_UNSUPPORTED_AT_ALL = 3

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

AT_FDCWD_HEX = 0x00000000FFFFFF9C  # = AT_FDCWD (hex)
AT_FDCWD_DEC = -100                # = AT_FDCWD (dec)

MAX_DEC_FD = 0x10000000


########################################################################################################################
# realpath -- get the resolved path (it does not resolve links YET)
########################################################################################################################
# noinspection PyShadowingBuiltins
def realpath(path):
    len_path = len(path)

    if len_path == 0:  # path is empty when BPF error occurs
        return ""

    assert(path[0] == '/')

    newpath = "/"
    newdirs = []
    dirs = path.split('/')

    for dir in dirs:
        if dir in ("", "."):
            continue

        if dir == "..":
            len_newdirs = len(newdirs)
            if len_newdirs > 0:
                del newdirs[len_newdirs - 1]
            continue

        newdirs.append(dir)

    newpath += "/".join(newdirs)

    if path[len_path - 1] == '/':
        newpath += "/"

    return newpath


########################################################################################################################
# ListSyscalls
########################################################################################################################
class ListSyscalls(list):
    def __init__(self, pmem_paths, script_mode, debug_mode, verbose_mode, init_pmem=0):

        list.__init__(self)

        self.log_anls = logging.getLogger("analysis")

        self.script_mode = script_mode
        self.debug_mode = debug_mode
        self.verbose_mode = verbose_mode

        self.print_progress = not (self.debug_mode or self.script_mode)

        self.time0 = 0

        self.pid_table = []
        self.npids = 0
        self.last_pid = -1
        self.last_pid_ind = 0

        self.fd_tables = []
        self.cwd_table = []

        self.all_strings = ["(stdin)", "(stdout)", "(stderr)"]
        self.path_is_pmem = [0, 0, 0]

        if init_pmem and pmem_paths:
            paths = str(pmem_paths)
            pmem_paths = paths.split(':')

            # remove all empty strings
            while pmem_paths.count(""):
                pmem_paths.remove("")

            # add slash at the end and normalize all paths
            self.pmem_paths = [realpath(path + "/") for path in pmem_paths]

        else:
            self.pmem_paths = []

        self.all_supported = 1  # all syscalls are supported

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

    ####################################################################################################################
    def is_path_pmem(self, string):
        string = str(string)
        for path in self.pmem_paths:
            if string.find(path) == 0:
                return 1
        return 0

    ####################################################################################################################
    # all_strings_append -- append the string to the list of all strings
    ####################################################################################################################
    def all_strings_append(self, string, is_pmem):
        if self.all_strings.count(string) == 0:
            self.all_strings.append(string)
            self.path_is_pmem.append(is_pmem)
            str_ind = len(self.all_strings) - 1
        else:
            str_ind = self.all_strings.index(string)
        return str_ind

    ####################################################################################################################
    @staticmethod
    def fd_table_assign(table, fd, val):
        fd_table_length = len(table)

        assert_msg(fd_table_length + 1000 > fd, "abnormally huge file descriptor ({0:d}), input file may be corrupted"
                   .format(fd))

        for i in range(fd_table_length, fd + 1):
            table.append(-1)
        table[fd] = val

    ####################################################################################################################
    def print_always(self):
        for syscall in self:
            syscall.print_always()

    ####################################################################################################################
    # look_for_matching_record -- look for matching record in a list of incomplete syscalls
    ####################################################################################################################
    def look_for_matching_record(self, info_all, pid_tid, sc_id, name, retval):
        for syscall in self:
            check = syscall.check_read_data(info_all, pid_tid, sc_id, name, retval, DEBUG_OFF)
            if check == CHECK_OK:
                self.remove(syscall)
                return syscall
        return -1

    ####################################################################################################################
    # set_pid_index -- set PID index and create a new FD table for each PID
    ####################################################################################################################
    def set_pid_index(self, syscall):
        pid = syscall.pid_tid >> 32
        if pid != self.last_pid:
            self.last_pid = pid

            if self.pid_table.count(pid) == 0:
                self.pid_table.append(pid)
                self.npids = len(self.pid_table)

                self.fd_tables.append([0, 1, 2])
                self.cwd_table.append(self.cwd_table[self.last_pid_ind])

                if self.npids > 1:
                    self.log_anls.debug("DEBUG WARNING(set_pid_index): added new _empty_ FD table for new PID 0x{0:08X}"
                                        .format(pid))
                self.last_pid_ind = len(self.pid_table) - 1
            else:
                self.last_pid_ind = self.pid_table.index(pid)

        syscall.pid_ind = self.last_pid_ind

    ####################################################################################################################
    def set_pid_index_offline(self):
        length = len(self)

        if not self.script_mode:
            print("\nCounting PIDs:")

        for syscall in self:
            if self.print_progress:
                n = self.index(syscall) + 1
                print("\r{0:d} of {1:d} ({2:d}%) ".format(n, length, int((100 * n) / length)), end='')
            self.set_pid_index(syscall)

        if self.print_progress:
            print(" done.")

        if self.debug_mode:
            for pid in self.pid_table:
                self.log_anls.debug("PID[{0:d}] = 0x{1:016X}".format(self.pid_table.index(pid), pid))

    ####################################################################################################################
    # arg_is_pmem -- check if a path argument is located on the pmem filesystem
    ####################################################################################################################
    def arg_is_pmem(self, syscall, narg):
        if narg > syscall.nargs:
            return 0

        narg -= 1

        if syscall.has_mask(Arg_is_path[narg] | Arg_is_fd[narg]):
            str_ind = syscall.args[narg]
            if str_ind != -1 and str_ind < len(self.path_is_pmem) and self.path_is_pmem[str_ind]:
                return 1
        return 0

    ####################################################################################################################
    # check_fallocate_flags -- check if the fallocate flags are supported by pmemfile
    ####################################################################################################################
    @staticmethod
    def check_fallocate_flags(syscall):
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

    ####################################################################################################################
    # check_fcntl_flags -- check if the fcntl flags are supported by pmemfile
    ####################################################################################################################
    @staticmethod
    def check_fcntl_flags(syscall):
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

    ####################################################################################################################
    # is_supported -- check if the syscall is supported by pmemfile
    ####################################################################################################################
    def is_supported(self, syscall):
        # SyS_fork and SyS_vfork are not supported at all
        if syscall.name in ("fork", "vfork"):
            return RESULT_UNSUPPORTED_AT_ALL

        # SyS_clone is supported only with flags set by pthread_create()
        if syscall.name == "clone" and syscall.args[0] != F_PTHREAD_CREATE:
            syscall.unsupported_flag = "flags other than set by pthread_create()"
            return RESULT_UNSUPPORTED_FLAG

        # the rest of checks is valid only if at least one of paths or file descriptors points at pmemfile filesystem
        if not syscall.is_pmem:
            return RESULT_SUPPORTED

        # SyS_open and SyS_openat with O_ASYNC flag are not supported
        if syscall.has_mask(EM_rfd):
            # In order to make the checks faster, first the bit flags are checked (because it is very fast)
            # and a syscall name is verified only if all flags are correct (because comparing strings is slower).
            if (syscall.is_mask(Arg_is_path[0]) and syscall.args[1] & FLAG_O_ASYNC and syscall.name == "open") or \
               (syscall.is_mask(Arg_is_fd[0] | Arg_is_path[1] | EM_fileat) and syscall.args[2] & FLAG_O_ASYNC and
               syscall.name == "openat"):
                syscall.unsupported_flag = "O_ASYNC"
                return RESULT_UNSUPPORTED_FLAG
            return RESULT_SUPPORTED

        # let's check SyS_*at syscalls
        if syscall.is_mask(EM_isfileat):
            # SyS_execveat and SyS_name_to_handle_at are not supported
            if syscall.name in ("execveat", "name_to_handle_at"):
                return RESULT_UNSUPPORTED_AT_ALL

            # SyS_renameat2 with RENAME_WHITEOUT flag is not supported
            if syscall.nargs == 5 and syscall.name in "renameat2" and syscall.args[4] & FLAG_RENAME_WHITEOUT:
                syscall.unsupported_flag = "RENAME_WHITEOUT"
                return RESULT_UNSUPPORTED_FLAG

            # the rest of SyS_*at syscalls is supported
            return RESULT_SUPPORTED

        # let's check syscalls with file descriptor as the first argument
        if syscall.has_mask(EM_fd_1):
            # SyS_fallocate with FALLOC_FL_COLLAPSE_RANGE and FALLOC_FL_ZERO_RANGE and FALLOC_FL_INSERT_RANGE flags
            # is not supported
            if syscall.name == "fallocate":
                return self.check_fallocate_flags(syscall)

            # many of SyS_fcntl flags is not supported
            if syscall.name == "fcntl":
                return self.check_fcntl_flags(syscall)

        # let's check syscalls with pmem path or file descriptor as the first argument
        if self.arg_is_pmem(syscall, 1):
            # the following syscalls are not supported
            if syscall.name in (
                    "chroot", "execve", "readahead",
                    "setxattr", "lsetxattr", "fsetxattr",
                    "listxattr", "llistxattr", "flistxattr",
                    "removexattr", "lremovexattr", "fremovexattr"):
                return RESULT_UNSUPPORTED_AT_ALL

            # the SyS_flock syscall is not supported YET
            if syscall.name == "flock":
                return RESULT_UNSUPPORTED_YET

        # SyS_copy_file_range and SyS_splice syscalls are not supported YET
        if (self.arg_is_pmem(syscall, 1) or self.arg_is_pmem(syscall, 3)) and\
           syscall.name in ("copy_file_range", "splice"):
            return RESULT_UNSUPPORTED_YET

        # SyS_sendfile and SyS_sendfile64 syscalls are not supported YET
        if (self.arg_is_pmem(syscall, 1) or self.arg_is_pmem(syscall, 2)) and\
           syscall.name in ("sendfile", "sendfile64"):
            return RESULT_UNSUPPORTED_YET

        # SyS_mmap syscall is not supported YET
        if self.arg_is_pmem(syscall, 5) and syscall.name == "mmap":
            return RESULT_UNSUPPORTED_YET

        # the rest of syscalls is supported
        return RESULT_SUPPORTED

    ####################################################################################################################
    def log_print_path(self, is_pmem, name, path):
        if is_pmem:
            self.log_anls.debug("{0:20s} \"{1:s}\" [PMEM]".format(name, path))
        else:
            self.log_anls.debug("{0:20s} \"{1:s}\"".format(name, path))

    ####################################################################################################################
    @staticmethod
    def log_build_msg(msg, is_pmem, path):
        if is_pmem:
            msg += " \"{0:s}\" [PMEM]".format(path)
        else:
            msg += " \"{0:s}\"".format(path)
        return msg

    ####################################################################################################################
    def set_first_cwd(self, cwd):
        assert_msg(len(self.cwd_table) == 0, "cwd_table is not empty")
        self.cwd_table.append(cwd)

    ####################################################################################################################
    def set_cwd(self, new_cwd, syscall):
        self.cwd_table[syscall.pid_ind] = new_cwd

    ####################################################################################################################
    def get_cwd(self, syscall):
        return self.cwd_table[syscall.pid_ind]

    ####################################################################################################################
    def get_fd_table(self, syscall):
        return self.fd_tables[syscall.pid_ind]

    ####################################################################################################################
    # handle_fileat -- helper function of match_fd_with_path() - handles *at syscalls
    #    syscall        - current syscall
    #    arg1           - number of the argument with a file descriptor
    #    arg2           - number of the argument with a path
    #    msg            - message to be printed out built so far
    #    has_to_be_pmem - if set, force the new path to be pmem (in case of SyS_symlinkat)
    ####################################################################################################################
    def handle_fileat(self, syscall, arg1, arg2, msg, has_to_be_pmem):
        assert_msg(syscall.has_mask(Arg_is_fd[arg1]), "argument #{0:d} is not a file descriptor".format(arg1))
        assert_msg(syscall.has_mask(Arg_is_path[arg2]), "argument #{0:d} is not a path".format(arg2))

        dirfd = syscall.args[arg1]
        if dirfd == AT_FDCWD_HEX:
            dirfd = AT_FDCWD_DEC

        # check if AT_EMPTY_PATH is set
        if (syscall.has_mask(EM_aep_arg_4) and (syscall.args[3] & AT_EMPTY_PATH)) or\
           (syscall.has_mask(EM_aep_arg_5) and (syscall.args[4] & AT_EMPTY_PATH)):
            path = ""
        else:
            path = syscall.strings[syscall.args[arg2]]

        dir_str = ""
        newpath = path
        unknown_dirfd = 0

        # handle empty and relative paths
        if (len(path) == 0 and not syscall.read_error) or (len(path) != 0 and path[0] != '/'):
            # get FD table of the current PID
            fd_table = self.get_fd_table(syscall)

            # check if dirfd == AT_FDCWD
            if dirfd == AT_FDCWD_DEC:
                dir_str = self.get_cwd(syscall)

            # is dirfd saved in the FD table?
            elif 0 <= dirfd < len(fd_table):
                # read string index of dirfd
                str_ind = fd_table[dirfd]
                # save string index instead of dirfd as the argument
                syscall.args[arg1] = str_ind
                # read path of dirfd
                dir_str = self.all_strings[str_ind]

            elif syscall.has_mask(EM_rfd) and syscall.iret != -1:
                unknown_dirfd = 1

            if not unknown_dirfd:
                if len(path) == 0:
                    newpath = dir_str
                else:
                    newpath = dir_str + "/" + path

        if dir_str != "":
            msg += " \"{0:s}\" \"{1:s}\"".format(dir_str, path)
        else:
            msg += " ({0:d}) \"{1:s}\"".format(dirfd, path)

        path = newpath

        if not unknown_dirfd:
            is_pmem = self.is_path_pmem(realpath(path))
        else:
            is_pmem = 0

        is_pmem |= has_to_be_pmem

        # append new path to the global array of all strings
        str_ind = self.all_strings_append(path, is_pmem)
        # save index in the global array as the argument
        syscall.args[arg2] = str_ind
        syscall.is_pmem |= is_pmem

        if is_pmem:
            msg += " [PMEM]"

        if unknown_dirfd:
            self.log_anls.warning("Unknown dirfd : {0:d}".format(dirfd))

        return path, is_pmem, msg

    ####################################################################################################################
    # handle_one_path -- helper function of match_fd_with_path() - handles one path argument of number n
    ####################################################################################################################
    def handle_one_path(self, syscall, n):
        path = syscall.strings[syscall.args[n]]

        if syscall.read_error and len(path) == 0:
            is_pmem = 0
        else:
            # handle relative paths
            if len(path) == 0:
                path = self.get_cwd(syscall)
            elif path[0] != '/':
                path = self.get_cwd(syscall) + "/" + path

            is_pmem = self.is_path_pmem(realpath(path))
            syscall.is_pmem |= is_pmem

        # append new path to the global array of all strings
        str_ind = self.all_strings_append(path, is_pmem)

        # save index in the global array as the argument
        syscall.args[n] = str_ind

        return path, str_ind, is_pmem

    ####################################################################################################################
    # match_fd_with_path -- save paths in the table and match file descriptors with saved paths
    ####################################################################################################################
    def match_fd_with_path(self, syscall):
        if syscall.read_error:
            self.log_anls.warning("BPF read error occurred, path is empty in syscall: {0:s}".format(syscall.name))

        # handle SyS_open or SyS_creat
        if syscall.is_mask(EM_fd_from_path):
            path, str_ind, is_pmem = self.handle_one_path(syscall, 0)
            self.log_print_path(is_pmem, syscall.name, path)

            fd_out = syscall.iret
            if fd_out != -1:
                # get FD table of the current PID
                fd_table = self.get_fd_table(syscall)
                # add to the FD table new pair (fd_out, str_ind):
                # - new descriptor 'fd_out' points at the string of index 'str_ind' in the table of all strings
                self.fd_table_assign(fd_table, fd_out, str_ind)

        # handle SyS_symlinkat
        elif syscall.name == "symlinkat":
            msg = "{0:20s}".format("symlinkat")
            target_path, str_ind, target_is_pmem = self.handle_one_path(syscall, 0)
            msg = self.log_build_msg(msg, target_is_pmem, target_path)
            link_path, link_is_pmem, msg = self.handle_fileat(syscall, 1, 2, msg, target_is_pmem)
            self.log_anls.debug(msg)
            if link_is_pmem and syscall.iret == 0:
                self.pmem_paths.append(link_path)
                self.log_anls.debug("INFO: new symlink added to pmem paths: \"{0:s}\"".format(link_path))

        # handle the rest of SyS_*at syscalls
        elif syscall.is_mask(EM_isfileat):
            msg = "{0:20s}".format(syscall.name)
            path, is_pmem, msg = self.handle_fileat(syscall, 0, 1, msg, 0)
            fd_out = syscall.iret

            # handle SyS_openat
            if syscall.has_mask(EM_rfd) and fd_out != -1:
                str_ind = self.all_strings_append(path, is_pmem)
                # get FD table of the current PID
                fd_table = self.get_fd_table(syscall)
                # add to the FD table new pair (fd_out, str_ind):
                # - new descriptor 'fd_out' points at the string of index 'str_ind' in the table of all strings
                self.fd_table_assign(fd_table, fd_out, str_ind)

            # handle syscalls with second 'at' pair (e.g. linkat, renameat)
            if syscall.is_mask(EM_isfileat2):
                path, is_pmem, msg = self.handle_fileat(syscall, 2, 3, msg, 0)

            self.log_anls.debug(msg)

        # handle SyS_dup*
        elif syscall.is_mask(EM_fd_from_fd):
            # get FD table of the current PID
            fd_table = self.get_fd_table(syscall)
            fd_in = syscall.args[0]
            fd_out = syscall.iret

            # is fd_in saved in the FD table?
            if 0 <= fd_in < len(fd_table):
                # read string index of fd_in
                str_ind = fd_table[fd_in]
                # save string index instead of fd_in as the argument
                syscall.args[0] = str_ind
                # read path of fd_in
                path = self.all_strings[str_ind]
                is_pmem = self.path_is_pmem[str_ind]
                syscall.is_pmem |= is_pmem
                self.log_print_path(is_pmem, syscall.name, path)

                if fd_out != -1:
                    # add to the FD table new pair (fd_out, str_ind):
                    # - new descriptor 'fd_out' points at the string of index 'str_ind' in the table of all strings
                    self.fd_table_assign(fd_table, fd_out, str_ind)
            else:
                # fd_in is an unknown descriptor
                syscall.args[0] = -1
                self.log_anls.debug("{0:20s} ({1:d})".format(syscall.name, fd_in))

                if fd_out != -1:
                    self.log_anls.warning("Unknown fd : {0:d}".format(fd_in))

        # handle SyS_close
        elif syscall.name == "close":
            fd_in = syscall.args[0]
            # get FD table of the current PID
            fd_table = self.get_fd_table(syscall)

            # is fd_in saved in the FD table?
            if 0 <= fd_in < len(fd_table):
                # read string index of fd_in
                str_ind = fd_table[fd_in]
                # "close" the fd_in descriptor
                fd_table[fd_in] = -1
                # read path of fd_in
                path = self.all_strings[str_ind]
                is_pmem = self.path_is_pmem[str_ind]
                syscall.is_pmem |= is_pmem
                self.log_print_path(is_pmem, syscall.name, path)
            else:
                self.log_anls.debug("{0:20s} (0x{1:016X})".format(syscall.name, fd_in))

        # handle syscalls with a path or a file descriptor among arguments
        elif syscall.has_mask(EM_str_all | EM_fd_all):
            msg = "{0:20s}".format(syscall.name)

            # loop through all syscall's arguments
            for narg in range(syscall.nargs):

                # check if the argument is a string
                if syscall.has_mask(Arg_is_str[narg]):
                    is_pmem = 0
                    path = syscall.strings[syscall.args[narg]]

                    # check if the argument is a path
                    if syscall.has_mask(Arg_is_path[narg]):
                        # handle relative paths
                        if len(path) != 0 and path[0] != '/':
                            self.all_strings_append(path, 0)  # add relative path as non-pmem
                            path = self.get_cwd(syscall) + "/" + path

                        # handle empty paths
                        elif len(path) == 0 and not syscall.read_error:
                            path = self.get_cwd(syscall)

                        is_pmem = self.is_path_pmem(realpath(path))
                        syscall.is_pmem |= is_pmem

                    # append new path to the global array of all strings
                    str_ind = self.all_strings_append(path, is_pmem)

                    # save index in the global array as the argument
                    syscall.args[narg] = str_ind

                    msg = self.log_build_msg(msg, is_pmem, path)

                # check if the argument is a file descriptor
                if syscall.has_mask(Arg_is_fd[narg]):
                    # get FD table of the current PID
                    fd_table = self.get_fd_table(syscall)
                    fd = syscall.args[narg]

                    if fd in (0xFFFFFFFF, 0xFFFFFFFFFFFFFFFF):
                        fd = -1

                    # is fd saved in the FD table?
                    if 0 <= fd < len(fd_table):
                        # read string index of fd
                        str_ind = fd_table[fd]
                        # read path of fd
                        path = self.all_strings[str_ind]
                        is_pmem = self.path_is_pmem[str_ind]
                        syscall.is_pmem |= is_pmem
                        # save string index instead of fd as the argument
                        syscall.args[narg] = str_ind
                        msg = self.log_build_msg(msg, is_pmem, path)
                    else:
                        # fd_in is an unknown descriptor
                        syscall.args[narg] = -1

                        if fd < MAX_DEC_FD:
                            msg += " ({0:d})".format(fd)
                        else:
                            msg += " (0x{0:016X})".format(fd)

            self.log_anls.debug(msg)

        self.post_match_action(syscall)

    ####################################################################################################################
    def post_match_action(self, syscall):
        # change current working directory in case of SyS_chdir and SyS_fchdir
        if syscall.ret == 0 and syscall.name in ("chdir", "fchdir"):
            old_cwd = self.get_cwd(syscall)
            new_cwd = self.all_strings[syscall.args[0]]
            self.set_cwd(new_cwd, syscall)
            self.log_anls.debug("INFO: current working directory changed:")
            self.log_anls.debug("      from: \"{0:s}\"".format(old_cwd))
            self.log_anls.debug("      to:   \"{0:s}\"".format(new_cwd))

        # add new PID to the table in case of SyS_fork, SyS_vfork and SyS_clone
        if syscall.name in ("fork", "vfork", "clone"):
            if syscall.iret <= 0:
                return
            old_pid = syscall.pid_tid >> 32
            new_pid = syscall.iret
            self.add_pid(new_pid, old_pid)

    ####################################################################################################################
    # add_pid -- add new PID to the table and copy CWD and FD table for this PID
    ####################################################################################################################
    def add_pid(self, new_pid, old_pid):
        if self.pid_table.count(new_pid) == 0:
            self.pid_table.append(new_pid)
            self.npids = len(self.pid_table)

            assert_msg(self.pid_table.count(old_pid) == 1, "there is no old PID in the table")

            old_pid_ind = self.pid_table.index(old_pid)
            self.cwd_table.append(self.cwd_table[old_pid_ind])
            self.fd_tables.append(self.fd_tables[old_pid_ind])
        else:
            # correct the CWD and FD table
            pid_ind = self.pid_table.index(new_pid)
            old_pid_ind = self.pid_table.index(old_pid)
            self.cwd_table[pid_ind] = self.cwd_table[old_pid_ind]
            self.fd_tables[pid_ind] = self.fd_tables[old_pid_ind]

        self.log_anls.debug("DEBUG Notice(add_pid): copied CWD and FD table from: "
                            "old PID 0x{0:08X} to: new PID 0x{1:08X}".format(old_pid, new_pid))

    ####################################################################################################################
    def match_fd_with_path_offline(self):
        assert_msg(len(self.cwd_table) > 0, "empty CWD table")

        if not self.script_mode:
            print("\nAnalyzing:")

        length = len(self)
        for syscall in self:
            if self.print_progress:
                n = self.index(syscall) + 1
                print("\r{0:d} of {1:d} ({2:d}%) ".format(n, length, int((100 * n) / length)), end='')

            if not self.has_entry_content(syscall):
                continue

            self.match_fd_with_path(syscall)
            syscall.unsupported_type = self.is_supported(syscall)

        if self.print_progress:
            print(" done.\n")

    ####################################################################################################################
    def has_entry_content(self, syscall):
        if not (syscall.content & CNT_ENTRY):  # no entry info (no info about arguments)
            if not (syscall.name in ("clone", "fork", "vfork") or syscall.sc_id == RT_SIGRETURN_SYS_EXIT):
                self.log_anls.warning("missing info about arguments of syscall: '{0:s}' - skipping..."
                                      .format(syscall.name))
            return 0
        return 1

    ####################################################################################################################
    def print_unsupported(self, l_names, l_inds):
        for name in l_names:
            if not self.verbose_mode:
                print("   {0:s}".format(name))
            else:
                list_ind = l_inds[l_names.index(name)]

                if len(list_ind):
                    print("   {0:s}:".format(name))
                else:
                    print("   {0:s}".format(name))

                for str_ind in list_ind:
                    if self.path_is_pmem[str_ind]:
                        print("\t\t\"{0:s}\" [PMEM]".format(self.all_strings[str_ind]))
                    else:
                        print("\t\t\"{0:s}\"".format(self.all_strings[str_ind]))

    ####################################################################################################################
    def print_unsupported_verbose2(self, msg, syscall, end):
        print("{0:28s}\t{1:16s}\t".format(msg, syscall.name), end='')
        for narg in range(syscall.nargs):
            if syscall.has_mask(Arg_is_path[narg] | Arg_is_fd[narg]):
                str_ind = syscall.args[narg]
                if str_ind != -1:
                    if self.path_is_pmem[str_ind]:
                        print(" \"{0:s}\" [PMEM]  ".format(self.all_strings[str_ind]), end='')
                    else:
                        print(" \"{0:s}\"".format(self.all_strings[str_ind]), end='')
        if end:
            print()

    ####################################################################################################################
    @staticmethod
    def add_to_unsupported_lists(syscall, name, l_names, l_inds):
        if l_names.count(name) == 0:
            l_names.append(name)
            ind = len(l_names) - 1
            list_ind = []
            l_inds.append(list_ind)
            assert_msg(len(l_names) == len(l_inds), "lists lengths are not equal")
        else:
            ind = l_names.index(name)
            list_ind = l_inds[ind]

        for narg in range(syscall.nargs):
            if syscall.has_mask(Arg_is_path[narg] | Arg_is_fd[narg]):
                str_ind = syscall.args[narg]
                if str_ind != -1:
                    if list_ind.count(str_ind) == 0:
                        list_ind.append(str_ind)

        l_inds[ind] = list_ind

    ####################################################################################################################
    def add_to_unsupported_lists_or_print(self, syscall):
        if not syscall.unsupported_type:
            return

        if self.all_supported:
            self.all_supported = 0

        if syscall.unsupported_type == RESULT_UNSUPPORTED_AT_ALL:
            if self.verbose_mode >= 2:
                self.print_unsupported_verbose2("unsupported syscall:", syscall, end=1)
            else:
                self.add_to_unsupported_lists(syscall, syscall.name, self.list_unsup, self.ind_unsup)

        elif syscall.unsupported_type == RESULT_UNSUPPORTED_FLAG:
            if self.verbose_mode >= 2:
                self.print_unsupported_verbose2("unsupported flag:", syscall, end=0)
                print(" [unsupported flag:]", syscall.unsupported_flag)
            else:
                name = syscall.name + " <" + syscall.unsupported_flag + ">"
                self.add_to_unsupported_lists(syscall, name, self.list_unsup_flag, self.ind_unsup_flag)

        else:  # syscall.unsupported_type == RESULT_UNSUPPORTED_YET
            if self.verbose_mode >= 2:
                self.print_unsupported_verbose2("unsupported syscall yet:", syscall, end=1)
            else:
                self.add_to_unsupported_lists(syscall, syscall.name, self.list_unsup_yet, self.ind_unsup_yet)

    ####################################################################################################################
    def print_unsupported_syscalls(self):
        if self.all_supported:
            print("All syscalls are supported.")
            return

        if self.verbose_mode >= 2:
            return

        # RESULT_UNSUPPORTED_AT_ALL
        if len(self.list_unsup):
            print("Unsupported syscalls detected:")
            self.print_unsupported(self.list_unsup, self.ind_unsup)
            print()

        # RESULT_UNSUPPORTED_FLAG
        if len(self.list_unsup_flag):
            print("Unsupported syscall's flag detected:")
            self.print_unsupported(self.list_unsup_flag, self.ind_unsup_flag)
            print()

        # RESULT_UNSUPPORTED_YET
        if len(self.list_unsup_yet):
            print("Yet-unsupported syscalls detected (will be supported):")
            self.print_unsupported(self.list_unsup_yet, self.ind_unsup_yet)
            print()

    ####################################################################################################################
    def print_unsupported_syscalls_offline(self):
        for syscall in self:
            self.add_to_unsupported_lists_or_print(syscall)
        self.print_unsupported_syscalls()
