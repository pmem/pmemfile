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


from os import path

from suite import Suite


class XfsTests(Suite):
    def __init__(self, install_dir, config):
        self.install_dir = install_dir
        self.create_local_config_file(config)
        config.process_switching = True
        super().__init__(config)

        self.timeout = 60 * 10
        self.cwd = install_dir

    def get_run_cmd(self, test):
        binary = path.join(self.install_dir, 'check')
        arg = 'generic/{}'.format(test)
        return [binary, arg]

    def prepare_default_tests_to_run(self):
        self.run_tests_from_file(path.join('xfstests', 'short_tests'))

    def create_local_config_file(self, config):
        local_config_path = path.join(self.install_dir, 'local.config')
        with open(local_config_path, 'w') as f:
            f.write('export FSTYP=tmpfs\n')
            f.write('export TEST_DEV=pmemfile:{}\n'.format(config.pf_pool))
            f.write('export TEST_DIR={}\n'.format(config.mountpoint))
            f.write('export TEST_DEVX={}\n'.format(config.pf_pool))
