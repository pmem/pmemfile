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

include(${SRC_DIR}/../preload-helpers.cmake)

setup()

mkfs(${DIR}/fs 16m)

execute_process(COMMAND ${CMAKE_COMMAND} -E make_directory ${DIR}/mount_point)
execute_process(COMMAND ${CMAKE_COMMAND} -E make_directory ${DIR}/some_dir)
execute_process(COMMAND ln -s ../mount_point ${DIR}/some_dir/some_link)

set(ENV{LD_PRELOAD} ${PRELOAD_LIB})
set(ENV{PMEMFILE_POOLS} ${DIR}/mount_point:${DIR}/fs)
set(ENV{PMEMFILE_PRELOAD_LOG} ${BIN_DIR}/pmemfile_preload.log)
set(ENV{INTERCEPT_LOG} ${BIN_DIR}/intercept.log)

mkdir_expect_failure(${DIR}/mount_point/nonexistent/dummy_dir_a)
mkdir(${DIR}/mount_point/test_dir)
mkdir(${DIR}/mount_point/test_dir_other)
mkdir(${DIR}/mount_point/test_dir/test_subdir)

execute(cp ${SRC_DIR}/repo_dummy_file_a ${DIR}/dummy_file_a)
execute(chmod 644 ${DIR}/dummy_file_a)
execute(cp ${SRC_DIR}/repo_dummy_file_b ${DIR}/dummy_file_b)
execute(chmod 644 ${DIR}/dummy_file_b)

execute(cp ${DIR}/dummy_file_a ${DIR}/mount_point/test_dir/file_a)
execute(cp ${DIR}/dummy_file_b ${DIR}/mount_point/test_dir/test_subdir/file_b)
execute(cp ${DIR}/mount_point/test_dir/file_a ${DIR}/file_a.log)
execute(cp ${DIR}/mount_point/test_dir/test_subdir/file_b ${DIR}/file_b.log)

cmp(${DIR}/file_a.log ${DIR}/dummy_file_a)
cmp(${DIR}/file_b.log ${DIR}/dummy_file_b)

execute(cp ${DIR}/mount_point/test_dir/./../test_dir/file_a ${DIR}/file_a.log)
execute(cp ${DIR}/mount_point/test_dir/test_subdir/../test_subdir/./file_b ${DIR}/file_b.log)

cmp(${DIR}/file_a.log ${DIR}/dummy_file_a)
cmp(${DIR}/file_b.log ${DIR}/dummy_file_b)


mkdir(${DIR}/mount_point/test_dir/a)
mkdir(${DIR}/mount_point/test_dir/a/b)
mkdir(${DIR}/mount_point/test_dir/a/b/c)
mkdir(${DIR}/mount_point/test_dir/a/b/c/d)
mkdir(${DIR}/mount_point/test_dir/a/b/c/d/e)
mkdir(${DIR}/mount_point/test_dir/a/b/c/d/e/f)
mkdir(${DIR}/mount_point/test_dir/a/b/c/d/e/f/g)
mkdir(${DIR}/mount_point/test_dir/a/b/c/d/e/f/g/h)
mkdir(${DIR}/mount_point/test_dir/a/b/c/d/e/f/g/h/i)
mkdir(${DIR}/mount_point/test_dir/a/b/c/d/e/f/g/h/i/j)
mkdir(${DIR}/mount_point/test_dir/a/b/c/d/e/f/g/h/i/j/k)
mkdir(${DIR}/mount_point/test_dir/a/b/c/d/e/f/g/h/i/j/k/l)
mkdir(${DIR}/mount_point/test_dir/a/b/c/d/e/f/g/h/i/j/k/l/m)
mkdir(${DIR}/mount_point/test_dir/a/b/c/d/e/f/g/h/i/j/k/l/m/n)

foreach(dir_index RANGE 0 64)
        mkdir(${DIR}/mount_point/test_dir/a/b/c/d/e/f/g/x${dir_index})
endforeach()

list_files(ls.log              ${DIR}/mount_point/test_dir)
list_files(ls_f.log            ${DIR}/mount_point/test_dir/a/b/c/d/e/f)
list_files(ls_a.log            ${DIR}/mount_point/test_dir/a)
list_files(ls_g.log            ${DIR}/mount_point/test_dir/a/b/c/d/e/f/g)
rmdir(${DIR}/mount_point/test_dir/a/b/c/d/e/f/g/x33)
list_files(ls_g_no33.log      ${DIR}/mount_point/test_dir/a/b/c/d/e/f/g)
rmdir(${DIR}/mount_point/test_dir/a/b/c/d/e/f/g/h/i/../../x32)
list_files(ls_g_no33_no32.log ${DIR}/mount_point/test_dir/a/b/c/d/e/f/g)

cleanup()
