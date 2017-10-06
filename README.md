# pmemfile

[![Build Status](https://travis-ci.org/pmem/pmemfile.svg)](https://travis-ci.org/pmem/pmemfile)
[![Coverage Status](https://codecov.io/github/pmem/pmemfile/coverage.svg)](https://codecov.io/gh/pmem/pmemfile)
[![Coverity Scan Build Status](https://scan.coverity.com/projects/12874/badge.svg)](https://scan.coverity.com/projects/pmemfile)

Pmemfile project's goal is to provide low-overhead userspace implementation of
file APIs using persistent memory.
It consists of 2 major compoments:
- libpmemfile-posix - provides syscall-like API which can be used by applications
- libpmemfile - provides transparent access to libpmemfile-posix pools

# How to build #

Requirements:
- cmake >= 3.3
- libpmemobj-dev(el) >= 1.3 (http://pmem.io/nvml/)
- libsyscall_intercept-dev(el) (https://github.com/pmem/syscall_intercept)

```sh
$ mkdir build
$ cd build
$ cmake .. -DCMAKE_INSTALL_PREFIX=/usr
$ make
$ make install
```

When developing:
```sh
$ ...
$ cmake .. -DCMAKE_BUILD_TYPE=Debug -DDEVELOPER_MODE=1 -DTEST_DIR=/mnt/pmem/pmemfile-tests
$ ...
$ ctest --output-on-failure
```

Note that in Debug mode "make install" installs only debug libraries.

Pmemfile-specific cmake variables:
* BUILD_LIBPMEMFILE=0 - disables building of libpmemfile.so
* BUILD_LIBPMEMFILE_POP=1 - builds tests using with libpmemfile-posix-over-POSIX library which imitates libpmemfile-posix.so, but uses POSIX functions provided by OS
* DEVELOPER_MODE=1 - enables coding style, whitespace, license checks and enables fail-on-warning flags
* LONG_TESTS=1 - enables tests which take much more time
* TEST_DIR=/mnt/pmem/test_dir - provides directory where tests will create its pools
* TRACE_TESTS=1 - dumps more info when test fails (requires cmake >= 3.4)
* TESTS_USE_FORCED_PMEM=1 - allows tests to force enable or force disable use of optimized flush in libpmemobj (to speed them up)
* USE_ASAN=1 - enables AddressSanitizer (only for debugging)
* USE_UBSAN=1 - enables UndefinedBehaviorSanitizer (only for debugging)
* LIBPMEMFILE_VALIDATE_POINTERS=1 - build-in support for PMEMFILE_PRELOAD_VALIDATE_POINTERS environment variable

# Package for Debian-based distros
```sh
$ ./utils/build-deb.sh
```
Resulting packages will be in ./build-deb/release and ./build-deb/debug

# Package for RPM-based distros
```sh
$ ./utils/build-rpm.sh
```
Resulting packages will be in ~/rpmbuild/RPMS.

Note: Before 1.0 pmemfile-posix API and ABI is considered unstable.
We reserve the right to change the on-media layout without warning.

# Description: #

# Example: #

```sh
$ mkfs.pmemfile /dev/dax0.0 0
$ mkdir /tmp/mountpoint

$ sudo pmemfile-mount /dev/dax0.0 /tmp/mountpoint
$ alias pf='LD_PRELOAD=libpmemfile.so'

# or if you don't have root access:

$ alias pf='LD_PRELOAD=libpmemfile.so PMEMFILE_POOLS=/tmp/mountpoint:/dev/dax0.0'

# now all commands prefixed with 'pf' will see files under /tmp/mountpoint,
# but files will be stored in file system backed by /dev/dax0.0
$ pf mkdir /tmp/mountpoint/dir_in_pmemfile
$ pf cp README.md /tmp/mountpoint/dir_in_pmemfile
$ pf ls -l /tmp/mountpoint/
total 0
drwxrwxrwx 2 user group 4008 Feb 16 17:46 dir_in_pmemfile
$ pf ls -l /tmp/mountpoint/dir_in_pmemfile
total 16
-rw-r--r-- 1 user group 1014 Feb 16 17:46 README.md
$ pf cat /tmp/mountpoint/dir_in_pmemfile/README.md | wc -c
1014
# verify that files are stored in pmemfile pool:
$ ls -l /tmp/mountpoint/
total 0
$ ls -l /tmp/mountpoint/dir_in_pmemfile
ls: cannot access '/tmp/mountpoint/dir_in_pmemfile': No such file or directory
```

# Limitations: #
* libpmemfile[-posix].so does not support multiple processes accessing the pool
  (libpmemobj limitation)
* libpmemfile.so supports Linux only (other UNIX-like systems could be supported)
* libpmemfile.so supports x86\_64 only (libsyscall_intercept, libpmem
  and libpmemobj limitation)
* libpmemfile.so is not safe with remotely replicated pool (libpmemfile-posix.so
  has no such limitation)

# Debugging: #
Environment variables:

libsyscall_intercept.so:
* INTERCEPT_LOG - log file for all intercepted syscalls (default: none)
* INTERCEPT_LOG_TRUNC - when set to 0, the above log file is not truncated
  (default: 1)
* INTERCEPT_HOOK_CMDLINE_FILTER - intercept syscalls only when process name
  matches this variable (used to skip gdb from intercepting) (default: empty)

libpmemobj.so:
* PMEMOBJ_LOG_FILE - log file (default: none)
* PMEMOBJ_LOG_LEVEL - logging level (default: 0)

libpmemfile-posix.so:
* PMEMFILE_POSIX_LOG_FILE - log file (default: none)
* PMEMFILE_POSIX_LOG_LEVEL - logging level (default: 0)

libpmemfile.so:
* PMEMFILE_PRELOAD_LOG - log file (default: none)
* PMEMFILE_PRELOAD_LOG_TRUNC - when set to 0, the above log file is not
  truncated (default: 1)
* PMEMFILE_EXIT_ON_NOT_SUPPORTED - when set to 1, aborts an application which
  uses unsupported syscall (default: 0)
* PMEMFILE_PRELOAD_PAUSE_AT_START - pauses initialization of pmemfile until
  debugger is attached (default: 0)

# Other variables: #
* PMEMFILE_BLOCK_SIZE - forces one block size (default: dynamic)
* PMEMFILE_CD - performs early chdir() to specified directory, used as
  a workaround for missing multi-process support when application must start
  from pmemfile-backed directory (default: none)
* PMEMFILE_IGNORE_INODE_FREE_ERRORS - when set to 1, disables abort() when
  freeing inode's metadata fails (it defers freeing to the next application
  start) - can be used to get out of out-of-space situations (default: 0)
* PMEMFILE_OVERALLOCATE_ON_APPEND - when set to 0, disables allocation of more
  space than required (default: 1)
* PMEMFILE_PRELOAD_PROCESS_SWITCHING - when set to 1, enables VERY slow
  emulation of multi-process support, used for testing pmemfile with file system
  test suites (default: 0)
* PMEMFILE_PRELOAD_VALIDATE_POINTERS - when set to 1, verifies memory reaching libpmemfile through syscall arguments is accessible; it's very slow, so it should never be used in production for non-buggy applications

# Other stuff #
* vltrace - tool for tracing applications and evaluating whether libpmemfile.so
  supports them (https://github.com/pmem/vltrace)
