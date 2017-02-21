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

set(ENV{LD_PRELOAD} libpmemfile.so)
set(ENV{PMEMFILE_POOLS} ${DIR}/mount_point:${DIR}/fs)
set(ENV{PMEMFILE_PRELOAD_LOG} ${BIN_DIR}/pmemfile_preload.log)
set(ENV{INTERCEPT_LOG} ${BIN_DIR}/intercept.log)
#set(ENV{PMEMFILE_EXIT_ON_NOT_SUPPORTED} 1)

execute(cp ${SRC_DIR}/repo_dummy_file_a ${DIR}/dummy_file_a)
execute(chmod 644 ${DIR}/dummy_file_a)
execute(cp ${SRC_DIR}/repo_dummy_file_b ${DIR}/dummy_file_b)
execute(chmod 644 ${DIR}/dummy_file_b)

execute(cp ${DIR}/dummy_file_a ${DIR}/mount_point/file_a)
execute(cp ${DIR}/dummy_file_b ${DIR}/mount_point/file_b)
execute(cp ${DIR}/mount_point/file_a ${DIR}/file_a.log)
execute(cp ${DIR}/mount_point/file_b ${DIR}/file_b.log)

cmp(${DIR}/file_a.log ${DIR}/dummy_file_a)
cmp(${DIR}/file_b.log ${DIR}/dummy_file_b)


execute(cp ${DIR}/dummy_file_a ${DIR}/mount_point/.file_a)
execute(cp ${DIR}/dummy_file_b ${DIR}/mount_point/.\#file_b)
execute(cp ${DIR}/mount_point/.file_a ${DIR}/file_a.log)
execute(cp ${DIR}/mount_point/.\#file_b ${DIR}/file_b.log)
execute(rm ${DIR}/mount_point/.file_a ${DIR}/mount_point/.\#file_b)

cmp(${DIR}/file_a.log ${DIR}/dummy_file_a)
cmp(${DIR}/file_b.log ${DIR}/dummy_file_b)


execute_with_output(${DIR}/file_a_cat.log cat ${DIR}/mount_point/file_a)
cmp(${DIR}/file_a_cat.log ${DIR}/dummy_file_a)

execute_with_output(${DIR}/pmemfile_ls_root.log ls ${DIR}/mount_point)
cmp(${DIR}/pmemfile_ls_root.log ${SRC_DIR}/ls_root_expected_log)

list_files(file_preload_lsl1.log ${DIR}/mount_point)

mkdir_expect_failure(${DIR}/mount_point/nonexistent/dummy_dir_a)

mkdir(${DIR}/mount_point/dummy_dir_a)
list_files(file_preload_lsl_with_dir1.log ${DIR}/mount_point)
rmdir(${DIR}/mount_point/dummy_dir_a)
list_files(file_preload_lsl_with_nodir1.log ${DIR}/mount_point)

# todo: when rm works...
#  only faccessat seems to be missing for rm to work
# expect_normal_exit rm ${DIR}/dummy_mount_point/file_a
# expect_normal_exit ls ${DIR}/dummy_mount_point > pmemfile_ls_root_after_rm.log
# cmp pmemfile_ls_root_after_rm.log ls_root_after_rm_expected_log

cleanup()
