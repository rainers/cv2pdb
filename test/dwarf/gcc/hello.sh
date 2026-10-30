set -ex
cp "$TESTDIR/hello.c" hello.c

gcc -g -o hello.exe hello.c
"$CV2PDB" hello.exe world.exe

ls -l hello* world*

"$CVDUMP" world.pdb >world.cvdump
grep '^S_PUB32: .*, Flags: 00000000, main$' world.cvdump
