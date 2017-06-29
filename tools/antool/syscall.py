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

from sys import stderr
import struct

FIRST_PACKET = 0  # this is the first packet for this syscall
LAST_PACKET = 7   # this is the last packet for this syscall
READ_ERROR = 1 << 10  # bpf_probe_read error occurred

E_KP_ENTRY = 0
E_KP_EXIT = 1
E_TP_ENTRY = 2
E_TP_EXIT = 3
E_MASK = 0x03

STATE_INIT = 0
STATE_IN_ENTRY = 1
STATE_ENTRY_COMPLETED = 2
STATE_COMPLETED = 3
STATE_CORRUPTED_ENTRY = 4
STATE_UNKNOWN_EVENT = 5

CNT_NONE = 0
CNT_ENTRY = 1
CNT_EXIT = 2

RESULT_SUPPORTED = 0
RESULT_UNSUPPORTED_YET = 1
RESULT_UNSUPPORTED_RELATIVE = 2
RESULT_UNSUPPORTED_FLAG = 3
RESULT_UNSUPPORTED = 4

CHECK_OK = 0
CHECK_SKIP = 1
CHECK_IGNORE = 2
CHECK_NO_ENTRY = 3
CHECK_NO_EXIT = 4
CHECK_WRONG_ID = 5
CHECK_WRONG_EXIT = 6
CHECK_SAVE_IN_ENTRY = 7

EM_fileat = 1 << 18  # '*at' type syscall (dirfd + path)
EM_fileat2 = 1 << 19  # double '*at' type syscall (dirfd + path)
EM_no_ret = 1 << 20  # syscall does not return
EM_rfd = 1 << 21  # syscall returns a file descriptor

def is_entry(etype):
    return (etype & 0x01) == 0

def is_exit(etype):
    return (etype & 0x01) == 1


class Syscall:
    __str = "---------------- ----------------"
    __arg_str_mask = [1, 2, 4, 8, 16, 32]

    ###############################################################################
    def __init__(self, pid_tid, sc_id, sc_info, buf_size, debug):
        self.debug_mode = debug
        self.state = STATE_INIT
        self.content = CNT_NONE

        self.pid_tid = pid_tid
        self.sc_id = sc_id
        self.time_start = 0
        self.time_end = 0
        self.args = []
        self.ret = 0
        self.iret = 0
        self.err = 0

        self.sc = sc_info
        self.name = sc_info.name
        self.mask = sc_info.mask

        self.string = ""
        self.num_str = 0
        self.str_fini = -1

        self.info_all = 0
        self.arg_first = FIRST_PACKET
        self.arg_last = LAST_PACKET
        self.is_cont = 0
        self.will_be_cont = 0
        self.read_error = 0

        self.truncated = 0

        self.strings = []
        self.str_is_path = []

        self.buf_size = int(buf_size)
        self.buf_size_2 = int(buf_size / 2)
        self.buf_size_3 = int(buf_size / 3)

        self.str_max_1 = self.buf_size - 2
        self.str_max_2 = self.buf_size_2 - 2
        self.str_max_3 = self.buf_size_3 - 2

        self.fmt_args = 'QQQQQQ'
        self.size_fmt_args = struct.calcsize(self.fmt_args)

        self.fmt_exit = 'q'
        self.size_fmt_exit = struct.calcsize(self.fmt_exit)

        self.pid_ind = -1
        self.is_pmem = 0
        self.unsupported = RESULT_SUPPORTED

    def __lt__(self, other):
        return self.time_start < other.time_start

    def is_mask(self, mask):
        return self.mask & mask == mask

    def has_mask(self, mask):
        return self.mask & mask

    def check_if_is_cont(self):
        return self.arg_first == self.arg_last

    ###############################################################################
    def is_string(self, n):
        if self.sc.mask & self.__arg_str_mask[n] == self.__arg_str_mask[n]:
            return 1
        else:
            return 0

    ###############################################################################
    def get_str_arg(self, n, aux_str):
        string = ""
        max_len = 0

        if self.info_all >> 2:
            max_len = self.str_max_1
            string = aux_str

        elif self.sc.nstrargs == 1:
            max_len = self.str_max_1
            string = aux_str

        elif self.sc.nstrargs == 2:
            max_len = self.str_max_2
            self.num_str += 1
            if self.num_str == 1:
                string = aux_str[0:self.buf_size_2]
            elif self.num_str == 2:
                string = aux_str[self.buf_size_2: 2 * self.buf_size_2]
            else:
                assert (self.num_str <= 2)

        elif self.sc.nstrargs == 3:
            max_len = self.str_max_3
            self.num_str += 1
            if self.num_str == 1:
                string = aux_str[0:self.buf_size_3]
            elif self.num_str == 2:
                string = aux_str[self.buf_size_3: 2 * self.buf_size_3]
            elif self.num_str == 3:
                string = aux_str[2 * self.buf_size_3: 3 * self.buf_size_3]
            else:
                assert (self.num_str <= 3)

        else:
            print("\n\nERROR: unsupported number of string arguments:", self.sc.nstrargs)
            assert (self.sc.nstrargs <= 3)

        str_p = str(string.decode(errors="ignore"))
        str_p = str_p.split('\0')[0]
        self.string += str_p

        # check if string ended
        if len(str_p) == (max_len + 1):
            # string did not ended
            self.str_fini = 0
            if self.will_be_cont == 0:
                # error: string is truncated
                self.truncated = n + 1
                self.str_fini = 1
        else:
            # string is completed, save it
            self.str_fini = 1

        if self.str_fini:
            self.strings.append(self.string)
            self.string = ""
            return len(self.strings) - 1
        else:
            return -1

    ###############################################################################
    def debug_print(self):
        if self.truncated:
            return

        if self.state not in (STATE_IN_ENTRY, STATE_ENTRY_COMPLETED, STATE_COMPLETED):
            print("DEBUG STATE =", self.state)

        if self.state == STATE_ENTRY_COMPLETED:
            self.print_entry()
        elif self.state == STATE_COMPLETED:
            if self.sc.mask & EM_no_ret:
                self.print_entry()
            else:
                self.print_exit()

    ###############################################################################
    def print_always(self):
        if self.debug_mode and self.state not in (STATE_ENTRY_COMPLETED, STATE_COMPLETED):
            print("DEBUG STATE =", self.state)

        self.print_entry()

        if (self.state == STATE_COMPLETED) and (self.sc.mask & EM_no_ret == 0):
            self.print_exit()

    ###############################################################################
    def print(self):
        if self.debug_mode:
            return
        self.print_always()

    ###############################################################################
    def print_entry(self):
        if not (self.content & CNT_ENTRY):
            return
        if self.read_error:
            print("Warning: BPF read error occurred, a string argument is empty in syscall:", self.name)
        print("{0:016X} {1:016X} {2:s} {3:s}".format(
            self.time_start, self.pid_tid, self.__str, self.name), end='')
        for n in range(0, self.sc.nargs):
            print(" ", end='')
            if self.is_string(n):
                if self.strings[self.args[n]] != "":
                    print("{0:s}".format(self.strings[self.args[n]]), end='')
                else:
                    print("\"\"", end='')
            else:
                print("{0:016X}".format(self.args[n]), end='')
        print()

        if self.sc.nstrargs != len(self.strings):
            print("self.sc.nstrargs =", self.sc.nstrargs)
            print("len(self.strings) =", len(self.strings))
            assert (self.sc.nstrargs == len(self.strings))

    ###############################################################################
    def print_exit(self):
        if not (self.content & CNT_EXIT):
            return
        if len(self.name) > 0:
            print("{0:016X} {1:016X} {2:016X} {3:016X} {4:s}".format(
                self.time_end, self.pid_tid, self.err, self.ret, self.name))
        else:
            print("{0:016X} {1:016X} {2:016X} {3:016X} sys_exit {4:016X}".format(
                self.time_end, self.pid_tid, self.err, self.ret, self.sc_id))

    def print_mismatch_info(self, etype, pid_tid, sc_id, name):
        print("Error: packet type mismatch: etype {0:d} while state {1:d}".format(etype, self.state))
        print("       previous syscall: {0:016X} {1:s} (sc_id:{2:d}) state {3:d}"
              .format(self.pid_tid, self.name, self.sc_id, self.state))
        print("        current syscall: {0:016X} {1:s} (sc_id:{2:d}) etype {3:d}"
              .format(pid_tid, name, sc_id, etype))

    ###############################################################################
    def do_check(self, info_all, pid_tid, sc_id, name, retval):

        etype = info_all & 0x03
        ret = CHECK_OK

        if pid_tid != self.pid_tid or sc_id != self.sc_id:
            ret = CHECK_WRONG_ID

        if self.state == STATE_INIT and is_exit(etype):
            if sc_id == 0xFFFFFFFFFFFFFFFF:  # 0xFFFFFFFFFFFFFFFF = sys_exit of rt_sigreturn
                return CHECK_OK
            if retval == 0 and name in ("clone", "fork", "vfork"):
                return CHECK_OK
            return CHECK_NO_ENTRY

        if self.state == STATE_IN_ENTRY and is_exit(etype):
            self.print_mismatch_info(etype, pid_tid, sc_id, name)
            return CHECK_SAVE_IN_ENTRY

        if self.state == STATE_ENTRY_COMPLETED:
            if is_entry(etype):
                if self.debug_mode and self.name not in ("clone", "fork", "vfork"):
                    print("Notice: exit info not found:", self.name)
                return CHECK_NO_EXIT
            elif is_exit(etype) and ret == CHECK_WRONG_ID:
                return CHECK_WRONG_EXIT

        if ret != CHECK_OK:
            self.print_mismatch_info(etype, pid_tid, sc_id, name)

        return ret

    ###############################################################################
    def add_data(self, info_all, bdata, timestamp):
        etype = info_all & E_MASK
        info_all &= ~E_MASK
        if etype == E_KP_ENTRY:
            if self.state not in (STATE_INIT, STATE_IN_ENTRY):
                print("Error: wrong state for etype == E_KP_ENTRY:", self.state, file=stderr)
            # kprobe entry handler
            return self.add_kprobe_entry(info_all, bdata, timestamp)
        elif (etype == E_KP_EXIT) or (etype == E_TP_EXIT):
            # kprobe exit handler or raw tracepoint sys_exit
            return self.add_exit(bdata, timestamp)
        else:
            return STATE_UNKNOWN_EVENT

    ###############################################################################
    def add_kprobe_entry(self, info_all, bdata, timestamp):
        self.time_start = timestamp

        if info_all & READ_ERROR:
            self.read_error = 1

        if info_all & ~READ_ERROR:
            self.info_all = info_all
            self.arg_first = (info_all >> 2) & 0x7  # bits 2-4
            self.arg_last = (info_all >> 5) & 0x7  # bits 5-7
            self.will_be_cont = (info_all >> 8) & 0x1  # bit 8 (will be continued)
            self.is_cont = (info_all >> 9) & 0x1  # bit 9 (is a continuation)

        if self.state == STATE_INIT and self.arg_first > FIRST_PACKET:
            print("Error: missed first packet of syscall :", self.name, file=stderr)
            print("       packet :", self.info_all, file=stderr)
            print("       arg_first :", self.arg_first, file=stderr)
            print("       arg_last :", self.arg_last, file=stderr)
            print("       will_be_cont :", self.will_be_cont, file=stderr)
            print("       is_cont :", self.is_cont, file=stderr)

        # is it a continuation of a string ?
        if self.check_if_is_cont():
            assert(self.is_cont == 1)
            if self.str_fini:
                return self.state
            if len(bdata) <= self.size_fmt_args:
                self.state = STATE_CORRUPTED_ENTRY
                return self.state
            aux_str = bdata[self.size_fmt_args:]

            str_p = str(aux_str.decode(errors="ignore"))
            str_p = str_p.split('\0')[0]

            self.string += str_p

            max_len = self.buf_size - 2
            # check if string ended
            if len(str_p) == (max_len + 1):
                # string did not ended
                self.str_fini = 0
                if self.will_be_cont == 0:
                    # error: string is truncated
                    self.truncated = len(self.args) + 1
                    self.str_fini = 1
            else:
                # string is completed, save it
                self.str_fini = 1

            if self.str_fini:
                self.strings.append(self.string)
                self.args.append(len(self.strings) - 1)
                self.string = ""

            return self.state

        # is it a continuation of last argument (full name mode)?
        if self.is_cont:
            # it is a continuation of the last string argument
            if self.str_fini:
                # printing string was already finished, so skip it
                self.arg_first += 1
                self.is_cont = 0
                self.str_fini = 0
        else:
            # syscall.arg_first argument was printed in the previous packet
            self.arg_first += 1

        # is it the last packet of this syscall (end of syscall) ?
        if self.arg_last == LAST_PACKET:
            end_of_syscall = 1
            # and set the true number of the last argument
            self.arg_last = self.sc.nargs
        else:
            end_of_syscall = 0

        data_args = bdata[0: self.size_fmt_args]
        aux_str = bdata[self.size_fmt_args:]

        if len(data_args) < self.size_fmt_args:
            if self.sc.nargs == 0:
                self.content = CNT_ENTRY
                self.state = STATE_ENTRY_COMPLETED
            else:
                self.state = STATE_CORRUPTED_ENTRY
            return self.state

        args = struct.unpack(self.fmt_args, data_args)

        for n in range((self.arg_first - 1), self.arg_last):
            if self.is_string(n):
                index = self.get_str_arg(n, aux_str)
                if index >= 0:
                    if len(self.args) < n + 1:
                        self.args.append(index)
                    else:
                        self.args[n] = index
            else:
                if len(self.args) < n + 1:
                    self.args.append(args[n])
                else:
                    self.args[n] = args[n]

        if end_of_syscall:
            self.num_str = 0  # reset counter of string arguments
            self.str_fini = 1
            self.content = CNT_ENTRY
            if self.sc.mask & EM_no_ret:  # SyS_exit and SyS_exit_group do not return
                self.state = STATE_COMPLETED
            else:
                self.state = STATE_ENTRY_COMPLETED
        else:
            self.state = STATE_IN_ENTRY

        return self.state

    ###############################################################################
    def get_ret(self, bdata):
        retval = -1
        if len(bdata) >= self.size_fmt_exit:
            bret = bdata[0: self.size_fmt_exit]
            retval, = struct.unpack(self.fmt_exit, bret)
        return retval

    ###############################################################################
    def add_exit(self, bdata, timestamp):
        if self.state == STATE_INIT:
            self.time_start = timestamp
        self.time_end = timestamp

        retval = self.get_ret(bdata)

        # split return value into result and errno
        if retval >= 0:
            self.ret = retval
            self.iret = retval
            self.err = 0
        else:
            self.ret = 0xFFFFFFFFFFFFFFFF
            self.iret = -1
            self.err = -retval

        self.content |= CNT_EXIT
        self.state = STATE_COMPLETED

        return self.state


