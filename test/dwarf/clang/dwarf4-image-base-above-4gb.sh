set -ex
cp "$TESTDIR/dwarf4-image-base-above-4gb.c" block.c

clang --target=x86_64-w64-mingw32 -fuse-ld=lld -g -gdwarf-4 -O2 -ffunction-sections -o block.exe block.c
clang --target=x86_64-w64-mingw32 -g -gdwarf-4 -O2 -ffunction-sections -S -o block.s block.c
awk '/DW_TAG_lexical_block$/ { getline; if ($NF == "DW_AT_ranges") r = $2 ":" } r && $1 == r { getline; if ($1 == ".quad" && $2 !~ /-/) found = 1 } END { exit !found }' block.s || { echo "test precondition lost: no lexical block with absolute ranges"; exit 1; }
llvm-objdump -p block.exe >block.headers
awk '$1 == "ImageBase" { found = strtonum("0x" $2) >= 2^32 } END { exit !found }' block.headers || { echo "test precondition lost: image base below 4 GB"; exit 1; }
"$CV2PDB" block.exe block-cv.exe
"$CVDUMP" -s block-cv.pdb >block-cv.dump

# The lexical blocks of main keep their locals v and w.
awk '$2 == "S_GPROC32:" { inmain = $NF == "main"; depth = 0 }
     inmain && $2 == "S_BLOCK32:" { depth++ }
     inmain && $2 == "S_END" { depth-- }
     $2 == "S_LOCAL:" { var = $NF; next }
     inmain && depth > 0 && $2 == "S_DEFRANGE_REGISTER_REL:" { found[var] = 1 }
     $2 ~ /^S_/ && $2 !~ /^S_DEFRANGE_/ { var = "" }
     END {exit !(found["v"] && found["w"]); }' block-cv.dump
