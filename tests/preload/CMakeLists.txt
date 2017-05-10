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

add_executable(preload_basic basic/basic.c)
add_executable(preload_config config/config.c)

add_cstyle(tests-preload-basic ${CMAKE_CURRENT_SOURCE_DIR}/basic/basic.c)
add_cstyle(tests-preload-config ${CMAKE_CURRENT_SOURCE_DIR}/config/config.c)

add_check_whitespace(tests-preload-basic ${CMAKE_CURRENT_SOURCE_DIR}/basic/basic.c)
add_check_whitespace(tests-preload-config ${CMAKE_CURRENT_SOURCE_DIR}/config/config.c)

function(add_test_generic name sub_name main_executable)
	add_test(NAME preload_${name}${sub_name}
		COMMAND ${CMAKE_COMMAND}
			${GLOBAL_TEST_ARGS}
			-DTEST_NAME=${name}
			-DSRC_DIR=${CMAKE_CURRENT_SOURCE_DIR}/${name}
			-DBIN_DIR=${CMAKE_CURRENT_BINARY_DIR}/${name}
			-DPRELOAD_LIB=$<TARGET_FILE:pmemfile_shared>
			-DEXTRA_ARG=${ARGN}
			-DMAIN_EXECUTABLE=${main_executable}
			-P ${CMAKE_CURRENT_SOURCE_DIR}/${name}/${name}.cmake)

	set_tests_properties(preload_${name}${sub_name} PROPERTIES
		ENVIRONMENT "LC_ALL=C;${ARGV3}")
endfunction()

find_program(SQLITE3 sqlite3)
if (SQLITE3)
	add_test_generic(sqlite "" none)
else()
	add_test(NAME sqlite_SKIPPED_BECAUSE_OF_MISSING_SQLITE3 COMMAND true)
endif()

add_test_generic(basic "" $<TARGET_FILE:preload_basic>)
add_test_generic(basic_commands "" none)
add_test_generic(nested_dirs "" none)


add_test_generic(config "_valid_via_symlink" $<TARGET_FILE:preload_config> some_dir/some_link/a)
set_tests_properties("preload_config_valid_via_symlink"
	PROPERTIES PASS_REGULAR_EXPRESSION "no error")
add_test_generic(config "_invalid" $<TARGET_FILE:preload_config> mount_point_wrong/a)
set_tests_properties("preload_config_invalid"
	PROPERTIES PASS_REGULAR_EXPRESSION "EIO")
add_test_generic(config "_invalid_plus_nested" $<TARGET_FILE:preload_config> mount_point_wrong/a/b/c)
set_tests_properties("preload_config_invalid_plus_nested"
	PROPERTIES PASS_REGULAR_EXPRESSION "EIO")
add_test_generic(config "_valid_plus_nested" $<TARGET_FILE:preload_config> mount_point/a/b)
set_tests_properties("preload_config_valid_plus_nested"
	PROPERTIES PASS_REGULAR_EXPRESSION "ENOENT")
add_test_generic(config "_invalid_via_valid" $<TARGET_FILE:preload_config> mount_point/./../mount_point_wrong/a)
set_tests_properties("preload_config_invalid_via_valid"
	PROPERTIES PASS_REGULAR_EXPRESSION "EIO")
add_test_generic(config "_outer_via_valid" $<TARGET_FILE:preload_config> mount_point/../a)
set_tests_properties("preload_config_outer_via_valid"
	PROPERTIES PASS_REGULAR_EXPRESSION "no error")
add_test_generic(config "_outer_via_invalid" $<TARGET_FILE:preload_config> mount_point_wrong/../a)
set_tests_properties("preload_config_outer_via_invalid"
	PROPERTIES PASS_REGULAR_EXPRESSION "EIO")
