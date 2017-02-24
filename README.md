# pmemfile

Pmemfile project's goal is to provide low-overhead userspace implementation of
file APIs using persistent memory.
It consists of 2 major compoments:
- libpmemfile-core - provides syscall-like API which can be used by applications
- libpmemfile - provides transparent access to libpmemfile-core pools

# How to build #

Requirements:
- cmake >= 3.3 (XXX: can we build with older versions?)
- libpmemobj-dev(el) >= 1.2 (http://pmem.io/nvml/)
- libsyscall_intercept-dev(el) (https://github.com/GBuella/syscall_intercept)

```sh
$ mkdir build
$ cd build
$ cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
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
```
* BUILD_LIBPMEMFILE=0 - disables building of libpmemfile.so
* DEVELOPER_MODE=1 - enables coding style, whitespace, license checks and enables fail-on-warning flags
* LONG_TESTS=1 - enables tests which take much more time
* TEST_DIR=/mnt/pmem/test_dir - provides directory where tests will create its pools
* TRACE_TESTS=1 - dumps more info when test fails (requires cmake >= 3.4)
```

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

Note: Before 1.0 pmemfile-core API and ABI is considered unstable.
We reserve the right to change the on-media layout without warning.

# Description: #

# Example: #

```sh
$ mkfs.pmemfile /mnt/pmem/pmemfile_pool 1G
$ mkdir /tmp/mountpoint
$ alias pf='LD_PRELOAD=libpmemfile.so PMEMFILE_POOLS=/tmp/mountpoint:/mnt/pmem/pmemfile_pool'
# now all commands prefixed with 'pf' will see files under /tmp/mountpoint,
# but files will be stored in file system backed by /mnt/pmem/pmemfile_pool
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
* libpmemfile[-core].so does not support multiple processes accessing the pool
  (libpmemobj limitation)
* libpmemfile.so supports Linux only (other UNIX-like systems could be supported)
* libpmemfile.so supports x86\_64 only (libsyscall_intercept limitation)

# Debugging: #
Environment variables:

libsyscall_intercept.so:
* INTERCEPT_LOG - log file for all intercepted syscalls
* LIBC_HOOK_CMDLINE_FILTER

libpmemobj.so:
* PMEMOBJ_LOG_FILE - log file
* PMEMOBJ_LOG_LEVEL - logging level

libpmemfile-core.so:
* PMEMFILECORE_LOG_FILE - log file
* PMEMFILECORE_LOG_LEVEL - logging level

libpmemfile.so:
* PMEMFILE_PRELOAD_LOG - log file for operations handled by libpmemfile-core
* PMEMFILE_EXIT_ON_NOT_SUPPORTED - when set to 1, aborts an application which
  uses currently unsupported syscalls

# Other stuff #
* strace.ebpf - tool for tracing applications and evaluating whether libpmemfile.so
  supports them (https://github.com/vitalyvch/strace.ebpf)
