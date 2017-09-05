#!/bin/bash -xe
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

USER_DIR=/home/user

LTP_SRC=$USER_DIR/ltp
TOP_BUILDDIR=$USER_DIR/buildltp
LTP_INSTALL_DIR=$USER_DIR/ltp_install

cd $USER_DIR
mkdir $TOP_BUILDDIR
mkdir $LTP_INSTALL_DIR

git clone https://github.com/linux-test-project/ltp.git
cd ltp
git checkout 20170516 # Release 16 may 2017
make autotools
test -d $TOP_BUILDDIR

cd $TOP_BUILDDIR
$LTP_SRC/configure --prefix=$LTP_INSTALL_DIR

make \
    -C $TOP_BUILDDIR \
    -f $LTP_SRC/Makefile \
    top_srcdir=$LTP_SRC \
    top_builddir=$TOP_BUILDDIR -j4

make \
    -C $TOP_BUILDDIR \
    -f $LTP_SRC/Makefile \
    top_srcdir=$LTP_SRC \
    top_builddir=$TOP_BUILDDIR \
    SKIP_IDCHECK=0 install -j4
