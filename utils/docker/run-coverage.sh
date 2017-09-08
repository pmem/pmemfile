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
# run-coverage.sh - is called inside a Docker container;
#		starts a build of PMEMFILE project
#

# Build all and run tests
cd $WORKDIR
cp /googletest-1.8.0.zip .

mkdir build
cd build
cmake .. -DDEVELOPER_MODE=1 \
		-DCMAKE_BUILD_TYPE=Debug \
		-DTEST_DIR=/tmp/pmemfile-tests \
		-DTRACE_TESTS=1 \
		-DTESTS_USE_FORCED_PMEM=1 \
		-DAUTO_GENERATE_SOURCES=$AUTOGENSOURCES \
		-DCMAKE_C_FLAGS=-coverage \
		-DCMAKE_CXX_FLAGS=-coverage

make -j2

git diff --exit-code || ( echo "Did you forget to commit generated source file?" && exit 1 )

function cleanup() {
	find . -name ".coverage" -exec rm {} \;
	find . -name "coverage.xml" -exec rm {} \;
	find . -name "*.gcov" -exec rm {} \;
	find . -name "*.gcda" -exec rm {} \;
}

cleanup

COVERAGE=1 PYTHON_SOURCE=$WORKDIR/src/tools/antool ctest -R antool --output-on-failure
REPORT=$(find . -name ".coverage") && [ $REPORT ] && mv -f -v $REPORT . && $(which python3) $(which coverage) xml
bash <(curl -s https://codecov.io/bash) -c -F tests_antool

cleanup

ctest -E "_memcheck|_drd|_helgrind|_pmemcheck|antool|preload|mt" --output-on-failure
bash <(curl -s https://codecov.io/bash) -c -F tests_posix_single_threaded

cleanup

ctest -E "_memcheck|_drd|_helgrind|_pmemcheck|antool" -R mt --output-on-failure
bash <(curl -s https://codecov.io/bash) -c -F tests_posix_multi_threaded

cleanup

ctest -E "_memcheck|_drd|_helgrind|_pmemcheck|antool" -R preload --output-on-failure
bash <(curl -s https://codecov.io/bash) -c -F tests_preload

cd ..
rm -r build
