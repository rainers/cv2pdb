set -ex
cp "$TESTDIR/data-public-flags.c" publics.c

clang --target=x86_64-w64-mingw32 -fuse-ld=lld -g -O0 -o publics.exe publics.c
"$CV2PDB" publics.exe publics-cv.exe
"$CVDUMP" -p publics-cv.pdb >publics-cv.dump

# Every public of a global variable has no flags, and neither do the publics of main and public_all.
for s in g_counter s_counter g_ratio g_pair g_name main public_all; do
  awk -v n=$s '$1 == "S_PUB32:" && $NF == n { found++; if ($4 != "00000000,") { print; bad++; } } END { exit !(found && !bad); }' publics-cv.dump
done
