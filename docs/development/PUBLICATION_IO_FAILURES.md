# Atomic publication under ENOSPC and EIO

Configure the standalone Linux build with SDB_IO_FAULT_TESTS=ON and run CTest
publication_io_failures. The interposer applies only to the worker process and
the unique temporary publication path inside its test directory. Failed write
and failed fsync both retained the prior file, removed the staging file and
allowed a later successful replacement on WSL GCC 13. This verifies injected
system-call failures; physical power-loss certification requires external tests.
