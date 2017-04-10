#!/bin/bash -ex
#
# Copyright 2016-2017, Intel Corporation
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
# install-valgrind.sh - installs valgrind for persistent memory
#

git clone --recursive --depth 1 https://github.com/pmem/valgrind.git
cd valgrind
./autogen.sh
./configure --prefix=/usr
make
make install
cd ..
rm -rf valgrind

rm -f /usr/lib*/valgrind/cachegrind-*
rm -f /usr/lib*/valgrind/callgrind-*
rm -f /usr/lib*/valgrind/exp-bbv-*
rm -f /usr/lib*/valgrind/exp-dhat-*
rm -f /usr/lib*/valgrind/exp-sgcheck-*
rm -f /usr/lib*/valgrind/getoff-*
rm -f /usr/lib*/valgrind/lackey-*
rm -f /usr/lib*/valgrind/massif-*
rm -f /usr/lib*/valgrind/none-*

rm -f /usr/lib*/valgrind/vgpreload_exp-dhat-*
rm -f /usr/lib*/valgrind/vgpreload_exp-sgcheck-*
rm -f /usr/lib*/valgrind/vgpreload_massif-*

rm -f /usr/lib*/valgrind/*.a
