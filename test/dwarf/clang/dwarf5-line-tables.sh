set -ex
cp "$TESTDIR/dwarf5-program.c" block.c

clang --target=x86_64-w64-mingw32 -fuse-ld=lld -g -gdwarf-5 -O2 -o block.exe block.c
clang --target=x86_64-w64-mingw32 -fuse-ld=lld -g -gdwarf-5 -O2 -ffunction-sections -o sections.exe block.c

# Both conversions read DWARF 5 line tables that have MD5 file checksums.
"$CV2PDB" block.exe block-cv.exe 2>&1 | tee block-cv.log
"$CV2PDB" sections.exe sections-cv.exe 2>&1 | tee sections-cv.log

# Neither conversion logs an error.
test -f block-cv.log
test -f sections-cv.log
if grep ERROR: block-cv.log sections-cv.log; then exit 1; fi
