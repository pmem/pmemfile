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


from os import path, linesep
from sys import exit


class Tester:
    def __init__(self, suite, config):
        self.suite = suite
        self.config = config
        self.failed_pf_only = []
        self.failed_local_only = []
        self.failed_both = []

    def test_pmemfile_against_local_fs(self, tc=None, verbose=False, timeout=None):
        """comparing test result on pmemfile and local filesystem"""
        if timeout:
            self.suite.timeout = timeout
        self.suite.verbose = verbose

        tests_to_run = [tc] if tc else self.suite.tests_to_run

        for test in tests_to_run:
            self.suite.run(test, on_pf=True)
            self.suite.run(test)
            print(self.suite)

        self.failed_pf_only = self.suite.failed_pf_only
        self.failed_local_only = self.suite.failed_local_only
        self.failed_both = self.suite.failed_all

        fails_count = len(self.failed_both) + len(self.failed_local_only) + \
            len(self.failed_pf_only)

        print("{0}SUMMARY{0}Out of {1} test cases {2} failed.".format(
            linesep, len(self.suite.ran_tests), fails_count))

        if self.failed_pf_only:
            print("{0}{1} failed only on pmemfile:".format(
                linesep, len(self.failed_pf_only)))
            for test in self.failed_pf_only:
                print(test)

        if self.failed_both:
            print("{0}{1} failed on pmemfile and local filesystem:".format(
                linesep, len(self.failed_both)))
            for test in self.failed_both:
                print(test)

        if self.failed_local_only:
            print("{0}{1} failed only on local filesystem:".format(
                linesep, len(self.failed_local_only)))
            for test in self.failed_local_only:
                print(test)
        print(linesep)

    def compare_with_past_execution(self, past_fails_path):
        exit_code = 0
        print(linesep)
        if not path.exists(past_fails_path):
            print('No file {}. No past execution to check against.'.format(
                past_fails_path))
            exit(exit_code)

        past_fails = self.suite.get_past_fails(past_fails_path)

        if set(self.failed_pf_only) == set(past_fails):
            print('No new failed tests.')
            exit(exit_code)

        new_fails = sorted(set(self.failed_pf_only) - set(past_fails))
        if new_fails:
            exit_code = 1
            print('New test failures introduced in this execution:')
            for fail in new_fails:
                print(fail)

            print('')
            self.suite.verbose = True
            for fail in new_fails:
                print('Rerunning test {0}:'.format(fail))
                self.suite.run(fail, on_pf=True)
                print('End out output for {0}\n'.format(fail));

        removed_fails = sorted(set(past_fails) -
                               (set(self.failed_pf_only) | set(self.failed_both)))
        if removed_fails:
            exit_code = 1
            print('Tests that stopped failing since last execution:')
            for removed in removed_fails:
                print(removed)

        exit(exit_code)
