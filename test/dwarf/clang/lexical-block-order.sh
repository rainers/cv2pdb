set -ex
cp "$TESTDIR/lexical-block-order.c" blocks.c

# In DWARF 4 assembly each block DIE names its code labels directly or through a .debug_ranges
# list, and the code labels appear in the file in address order.
clang --target=x86_64-w64-mingw32 -g -gdwarf-4 -O2 -S -o blocks.s blocks.c
awk 'FNR == NR && $1 == ".section" { sec = $2; next }
     FNR == NR && $1 ~ /^\.L[0-9A-Za-z_]+:$/ { lab = substr($1, 1, length($1) - 1); pos[lab] = ++nlab; next }
     FNR == NR && sec ~ /^\.debug_ranges/ && $1 == ".quad" && $2 != "0" { split($2, e, "-"); if (!(lab in first) || pos[e[1]] < first[lab]) first[lab] = pos[e[1]]; if (pos[e[1]] > last[lab]) last[lab] = pos[e[1]]; next }
     FNR == NR { next }
     $NF == "DW_TAG_lexical_block" && /Abbrev \[/ { getline; nblk++; ranges[nblk] = $NF == "DW_AT_ranges"; start[nblk] = ranges[nblk] ? first[$2] : pos[$2]; end[nblk] = ranges[nblk] ? last[$2] : pos[$2] }
     END {
       if (nblk != 4) { print "test precondition lost: target() does not have four lexical block DIEs"; exit 1 }
       if (start[1] <= start[2]) print "test precondition lost: the first block DIE (cold_slot) does not start above the second (hot_slot)"
       if (!ranges[2]) print "test precondition lost: the second block DIE (hot_slot) has no DW_AT_ranges"
       if (start[1] >= end[2]) print "test precondition lost: the first block DIE (cold_slot) does not start between the ranges of the second (hot_slot)"
       if (start[3] <= start[4]) print "test precondition lost: the first nested block DIE (inner_cold_slot) does not start above the second (inner_hot_slot)"
       exit !(start[1] > start[2] && ranges[2] && start[1] < end[2] && start[3] > start[4])
     }' blocks.s blocks.s

printf '1 hot_slot\n2 inner_hot_slot\n1 cold_slot\n1 hot_slot\n2 inner_cold_slot\n' >blocks.expected
for v in 4 5; do
  clang --target=x86_64-w64-mingw32 -fuse-ld=lld -g -gdwarf-$v -O2 -o blocks-d$v.exe blocks.c
  "$CV2PDB" blocks-d$v.exe blocks-cv-d$v.exe
  "$CVDUMP" -s blocks-cv-d$v.pdb >blocks-cv-d$v.dump
  # In `target()` the block of hot_slot has code on both sides of the block of cold_slot, so the
  # block of hot_slot is written as two S_BLOCK32 records.
  awk '$2 == "S_GPROC32:" { fn = $NF; d = 0; delete first; delete last }
       fn != "target" { next }
       $2 == "S_BLOCK32:" {
         split($3, a, /[:\]]/); o = strtonum("0x" a[2]); n = strtonum("0x" substr($5, 1, 8))
         if ((d in first) && o < first[d]) { print "S_BLOCK32 at " a[2] " follows a sibling at a higher address" >"/dev/stderr"; bad = 1 }
         else if ((d in last) && o < last[d]) { print "S_BLOCK32 at " a[2] " starts before the end of the previous sibling" >"/dev/stderr"; bad = 1 }
         first[d] = o; last[d] = o + n; d++; delete first[d]; delete last[d]
       }
       $2 == "S_REGREL32:" && d { print d, $NF }
       $2 == "S_END" { if (d) d--; else fn = "" }
       END { exit bad }' blocks-cv-d$v.dump >blocks-cv-d$v.locals
  diff blocks.expected blocks-cv-d$v.locals
done
