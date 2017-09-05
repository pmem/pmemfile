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


from os import path, environ, pathsep

from suite import Suite


class LinuxTestProject(Suite):

    def __init__(self, install_dir, config):
        self.install_dir = install_dir
        self.test_dir = path.join(install_dir, 'testcases', 'bin')
        self.test_suites_dir = path.join(install_dir, 'runtest')
        self.suppress_tconf_errors = True
        self.tmp_dir = '/tmp/'

        environ['PATH'] = self.test_dir + pathsep + environ['PATH']
        environ['LTPROOT'] = self.install_dir
        environ['TMPDIR'] = self.tmp_dir

        super().__init__(config)

    def get_run_cmd(self, test):
        if '|' in test:
            self.run_in_shell = True
            return test
        else:
            return path.join(self.test_dir, test).split()

    def run_tests_from_file(self, path):
        with open(path, 'r') as f:
            self.tests_to_run = [self.replace_env_vars(test.strip())
                                 for test in f.readlines() if not test.isspace()
                                 and not test.startswith('#')]

    def replace_env_vars(self, test):
        return test.replace('$LTPROOT', self.install_dir).replace('$TMPDIR', self.tmp_dir)

    def prepare_default_tests_to_run(self):
        self.run_tests_from_file(path.join('ltp', 'short_tests'))

    def get_process_error_result(self, process_error):
        tconf_return_code = 32
        if self.suppress_tconf_errors and process_error.returncode == tconf_return_code:
            return "PASSED"
        else:
            return "FAILED"

    def get_past_fails(self, past_fails_path):
        with open(past_fails_path, 'r') as f:
            return [self.replace_env_vars(fail.strip())
                    for fail in f.readlines() if fail.strip()]
