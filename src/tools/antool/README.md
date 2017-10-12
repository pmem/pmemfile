Antool - Analyzing Tool
=======================

Analyzing Tool analyzes binary logs of vltrace and checks
whether all recorded syscalls are supported by pmemfile.

Antool uses vltrace's bin2txt converter to read and parse its binary logs,
so the following files:

- converter.py
- exceptions.py
- syscallinfo.py
- syscall.py
- syscalltable.py
- utils.py

should be kept in sync in these two projects:

1) https://github.com/pmem/vltrace/tree/master/tools/bin2txt
2) https://github.com/pmem/pmemfile/tree/master/src/tools/antool


### CONTACTS ###

For more information about this tool contact:

 - Lukasz Dorau (lukasz.dorau at intel.com)

or create an issue [here](https://github.com/pmem/pmemfile/issues).
