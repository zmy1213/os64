# OS64 user programs

The `programs/*.cpp` tools run at CPL 3 in individual address spaces. They are
freestanding x86_64 ELF executables, use `int 0x80`, and require no host C library.
`make users` builds them; `make build` includes them under `/bin` on the data-disk
template. Existing `build/data.img` is preserved. `make reset-data` installs a
fresh template and removes any files saved in that data image.

Try these commands at the OS shell:

```text
ls /bin
run /bin/hello alpha beta
run /bin/echo hello world
run /bin/cat /readme.txt
run /bin/ls /docs
run /bin/writer /saved.txt saved_after_reboot
run /bin/fs_test /large.txt
run /bin/spawn_test
run /bin/spawn_test orphan
run /bin/badptr
run /bin/fault
run /bin/ud2
run /bin/sleep 100
run /bin/spin 10000000
ps
sync
reboot
run /bin/cat /saved.txt
run /bin/fs_test /large.txt verify
```

`fault` and `ud2` deliberately terminate their own processes. The shell continues
to run. `badptr` checks that system calls reject an invalid pointer without
terminating the process. `fs_test` checks eleven file blocks, including the
single-indirect block table; its optional third argument only verifies data.

The process entry stack is 16-byte aligned and contains `argc`, `argc` pointers
to null-terminated argument strings, and a null pointer. `start.asm` calls
`main(argc, argv)` and passes the result to `exit`.

The ABI uses RAX for the syscall number and RDI, RSI, RDX, RCX for arguments.
The result is a signed value in RAX. Numbers 0–11 retain the original teaching
ABI. Extensions are mkdir 12, getpid 13, sleep 14, spawn 15, waitpid 16, unlink 17,
sync 18, and open-with-flags 19. Open flags are READ 1, WRITE 2, CREATE 4, TRUNC 8,
and APPEND 16. Syscall 2 remains the legacy read-only open.

Each executable has separate RX and RW `PT_LOAD` segments and zero-filled BSS.
The compact linker layout keeps ELF files below the loader's 4096-byte staging
limit. Kernel and user code use general registers only while FPU/SIMD context
switching is unavailable.
