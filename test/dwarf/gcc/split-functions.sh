set -ex
cp "$TESTDIR/split-functions.c" split.c

gcc -g -O2 -shared -o split-dwarf.dll split.c
"$CV2PDB" split-dwarf.dll split-cv.dll

ls -l split-dwarf* split-cv*

"$CVDUMP" split-cv.pdb > split-cv.dump
awk '$2 == "S_GPROC32:" && $NF == "split_func" { scope=gensub(/,/,"",1,$3); } $2 == "S_SEPCODE:" { getline; if ($4 == scope) found=1; } END {exit !found; }' split-cv.dump
