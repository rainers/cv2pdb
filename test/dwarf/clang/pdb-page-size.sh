set -ex
cp "$TESTDIR/pdb-page-size.c" page.c

clang --target=x86_64-w64-mingw32 -fuse-ld=lld -g -O0 -o page.exe page.c
"$CV2PDB" page.exe page-cv.exe
"$CV2PDB" -b8192 page.exe page-8k.exe
"$CV2PDB" -b1024 page.exe page-1k.exe

# The page size of an MSF file is the 32-bit value after its 32-byte magic.
size=$(od -An -tu4 -j32 -N4 page-cv.pdb)
test $size -eq 4096
size=$(od -An -tu4 -j32 -N4 page-8k.pdb)
test $size -eq 8192
size=$(od -An -tu4 -j32 -N4 page-1k.pdb)
test $size -eq 1024

if "$CV2PDB" -b2048 page.exe page-2k.exe; then exit 1; fi
