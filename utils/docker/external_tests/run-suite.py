#!/usr/bin/env python3

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


import argparse
from time import time, gmtime

from config import Config
from ltp.ltp import LinuxTestProject
from sqlite.sqlite import Sqlite
from xfstests.xfs import XfsTests
from tester import Tester


def get_cmd_args():
    parser = argparse.ArgumentParser(
        description="Run test_suites tests with pmemfile.")

    subparsers = parser.add_subparsers()
    subparsers.dest = 'suite_name'
    subparsers.required = True

    parser_ltp = subparsers.add_parser(
        'ltp', help='Linux Test Project parser.')
    parser_ltp.add_argument(
        "--fail-on-tconf", help="TCONF errors (missing test preconditions) are treated as FAILED.",
        action='store_false')

    parser_sqlite = subparsers.add_parser('sqlite', help='Sqlite tests parser')
    parser_xfs = subparsers.add_parser('xfs', help='XfsTests parser')

    suite_parsers = [parser_ltp, parser_sqlite, parser_xfs]
    for suite_parser in suite_parsers:
        suite_parser.add_argument("-p", "--pf-pool", required=True,
                                  help="Path to pmemfile_pool.")
        suite_parser.add_argument("-m", "--mountpoint", required=True,
                                  help="Path to pmemfile mountpoint directory.")
        suite_parser.add_argument("-l", "--pf-lib-dir", required=True,
                                  help="Path to pmemfile installed lib directory.")
        suite_parser.add_argument(
            "-t", "--tests", help="File with list of test cases (in separate lines).")
        suite_parser.add_argument("-v", "--verbose", action='store_true',
                                  help="Print outputs of tests that failed.")
        suite_parser.add_argument(
            "-f", "--fails", help="Path to the file with past fails.")
        suite_parser.add_argument(
            "--timeout", type=int, default=15 * 60, help="Timeout for single test in seconds (default: 15 minutes).")
        suite_parser.add_argument("-i", "--install-dir", required=True,
                                  help="Path to suite installation directory.")

    return parser.parse_args()


if __name__ == "__main__":
    args = get_cmd_args()

    suites = {'ltp': LinuxTestProject,
              'sqlite': Sqlite,
              'xfs': XfsTests}

    config = Config(args.pf_lib_dir, args.pf_pool, args.mountpoint)
    suite = suites[args.suite_name](args.install_dir, config)

    if args.suite_name == 'ltp':
        suite.suppress_tconf_errors = args.fail_on_tconf

    if args.tests is not None:
        suite.run_tests_from_file(args.tests)
    else:
        suite.prepare_default_tests_to_run()

    tester = Tester(suite, config)

    start = time()
    tester.test_pmemfile_against_local_fs(args.verbose, args.timeout)
    elapsed = gmtime(time() - start)

    print("Total execution time: {0} hours, {1} minutes, {2} seconds.".format(
        elapsed.tm_hour, elapsed.tm_min, elapsed.tm_sec))

    if args.fails is not None:
        tester.compare_with_past_execution(args.fails)
