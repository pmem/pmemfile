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


from subprocess import check_output, STDOUT, TimeoutExpired, CalledProcessError
from os import linesep, environ, pathsep
from os.path import expanduser
from time import perf_counter
from abc import ABCMeta, abstractmethod
from collections import OrderedDict


class Suite(metaclass=ABCMeta):
    timeout = 15 * 60

    def __init__(self, config):
        self.verbose = False
        self.results = {}
        self.tests_to_run = []
        self.test = ''
        self.ran_tests = []
        self.config = config
        self.mountpoint = config.mountpoint
        self.run_in_shell = False
        self.cwd = config.mountpoint

        if not hasattr(self, 'suite_env_cmd'):
            self.suite_env_cmd = {}

    def __str__(self):
        if self.test not in self.results:
            return '{} not run'.format(self.test)
        config_margin = 35
        results = ['{0:>15} on {1} {2:>{3}.3f} [ms]'.
                   format(self.test_entry[config]['result'], config,
                          self.test_entry[config]['time'], config_margin - len(config))
                   for config in self.test_entry.keys()]

        printed_info = ''
        if (self.verbose):
            printed_info = "{0}{1}".format(
                self.get_equivalent_command(self.test), linesep)

        return '{0}:{1}{2}'.format(self.test, linesep, printed_info) + linesep.join(results)

    def get_equivalent_command(self, test):
        cmd_env = {}
        cmd_env.update(self.config.pf_env)
        cmd_env.update(self.suite_env_cmd)

        env_prefix = ' '.join(['{}={}'.format(env, env_val) for env, env_val in cmd_env.items(
        )]) + ' PMEMFILE_CD={}'.format(self.cwd)
        run_cmd = self.get_run_cmd(test)

        if isinstance(run_cmd, list):
            run_cmd = ' '.join(run_cmd)

        home = expanduser('~')
        return '{0} {1}'.format(env_prefix, run_cmd).replace(home, '~')

    @abstractmethod
    def get_run_cmd(self, test):
        """Provides list representing command executed in order to run a test."""
        return test

    @abstractmethod
    def prepare_default_tests_to_run(self):
        """Assigns default list of tests to self.tests_to_run."""
        pass

    def read_tests_from_file(self, path):
        with open(path, 'r') as f:
            return [test.split('#')[0].strip() for test in f.readlines() if not test.isspace()
                    and not test.startswith('#')]

    def run_tests_from_file(self, path):
        self.tests_to_run = self.read_tests_from_file(path)

    def get_past_fails(self, past_fails_path):
        return self.read_tests_from_file(past_fails_path)

    @property
    def failed_pf_only(self):
        return [test for test in self.ran_tests
                if self.results[test]['pmemfile']['result'] != 'PASSED'
                and all(self.results[test][config]['result'] == 'PASSED'
                        for config in self.results[test].keys() if config != 'pmemfile')]

    @property
    def failed_local_only(self):
        return [test for test in self.ran_tests
                if self.results[test]['local filesystem']['result'] != 'PASSED'
                and all(self.results[test][config]['result'] == 'PASSED'
                        for config in self.results[test].keys() if config != 'local filesystem')]

    @property
    def failed_all(self):
        return [test for test in self.ran_tests
                if all(self.results[test][config]['result'] != 'PASSED'
                       for config in self.results[test].keys())]

    @property
    def test_entry(self):
        return self.results[self.test]

    def get_process_error_result(self, process_error):
        """Called if process finished with exit code different than 0.
        May be overriden to provide more specific result based on finished process parameters."""
        return 'FAILED'

    def run(self, test, on_pf=False):
        self.test = test
        self.run_in_shell = False
        configuration = 'pmemfile' if on_pf else 'local filesystem'

        if test not in self.results:
            self.results[test] = OrderedDict()

        self.results[test][configuration] = {}

        output = ''

        start = perf_counter()
        try:
            output = self.try_decode(self.exec_test(on_pf))
        except TimeoutExpired as e:
            output = self.try_decode(e.output)
            self.results[test][configuration]['result'] = 'TIMEOUT'
        except CalledProcessError as e:
            output = self.try_decode(e.output)
            self.results[test][configuration]['result'] = self.get_process_error_result(
                e)
        except Exception as e:
            print('Unexpected Error.')
            output = e
            self.results[test][configuration]['result'] = 'ERROR'
        else:
            self.results[test][configuration]['result'] = 'PASSED'
        finally:
            elapsed = (perf_counter() - start) * 1000
            self.results[test][configuration]['time'] = elapsed
            if test not in self.ran_tests:
                self.ran_tests.append(test)
            if self.results[test][configuration]['result'] != 'PASSED':
                self.LOG(output)

    def exec_test(self, on_pf):
        cmd = self.get_run_cmd(self.test)
        env = self.config.all_env if on_pf else None
        return check_output(cmd, timeout=self.timeout,
                            stderr=STDOUT, env=env, cwd=self.cwd, shell=self.run_in_shell)

    def try_decode(self, output):
        try:
            return output.decode('utf-8')
        except UnicodeDecodeError:
            return "Error. Output couldn't be decoded."

    def add_env(self, env, value):
        if not hasattr(self, 'suite_env_cmd'):
            self.suite_env_cmd = {}

        if env in environ:
            self.suite_env_cmd[env] = '{0}:${{{1}}}'.format(value, env)
            value = value + pathsep + environ[env]
        else:
            self.suite_env_cmd[env] = value

        environ[env] = value

    def LOG(self, output):
        if self.verbose:
            print(output)
