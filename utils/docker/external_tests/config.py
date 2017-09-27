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


from os import path, environ


class Config:
    def __init__(self, pf_lib_dir, pf_pool, mountpoint):
        self._mountpoint = mountpoint
        self._pf_lib_dir = pf_lib_dir
        self._pf_pool = pf_pool
        self.process_switching = False
        self._pf_env = {
            'PMEM_IS_PMEM_FORCE': '1',
            'LD_PRELOAD': path.join(pf_lib_dir, 'libpmemfile.so'),
            'PMEMFILE_POOLS': '{0}:{1}'.format(self.mountpoint, pf_pool)
        }

    @property
    def all_env(self):
        self.update_env()
        return self._all_env

    @property
    def pf_env(self):
        if self.process_switching:
            self._pf_env.update({'PMEMFILE_PROCESS_SWITCHING': '1'})
        return self._pf_env

    @property
    def mountpoint(self):
        return self._mountpoint

    @property
    def pf_pool(self):
        return self._pf_pool

    def update_env(self):
        self._all_env = environ.copy()
        self._all_env.update(self._pf_env)
