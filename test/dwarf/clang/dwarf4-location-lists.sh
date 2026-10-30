set -ex
cp "$TESTDIR/dwarf4-location-lists.c" spill.c

clang --target=x86_64-w64-mingw32 -fuse-ld=lld -g -gdwarf-4 -O2 -ffunction-sections -o spill.exe spill.c
clang --target=x86_64-w64-mingw32 -g -gdwarf-4 -O2 -ffunction-sections -S -o spill.s spill.c
awk 'FNR == NR && $1 ~ /^\.Linfo_string[0-9]+:$/ { s = substr($1, 1, length($1) - 1); getline; str[s] = $2; next }
     FNR == NR && $1 == ".section" { inloc = $2 ~ /^\.debug_loc,/; next }
     FNR == NR && inloc && $1 ~ /^\.Ldebug_loc[0-9]+:$/ { l = substr($1, 1, length($1) - 1); getline; base[l] = $1 == ".quad" && $2 == "-1"; next }
     FNR == NR && inloc && $NF == "DW_OP_breg7" { getline; slots[l] = slots[l] " " $NF; next }
     FNR == NR { next }
     /Abbrev \[/ { tag = $NF; if (tag == "DW_TAG_subprogram") insub = 0 }
     tag == "DW_TAG_subprogram" && $NF == "DW_AT_name" { insub = str[$2] == "\"spill\"" }
     insub && tag == "DW_TAG_formal_parameter" && $NF == "DW_AT_location" { l = $2 }
     insub && tag == "DW_TAG_formal_parameter" && $NF == "DW_AT_name" { if (!base[l] || slots[l] == "") bad = 1; name = str[$2]; gsub(/"/, "", name); print name slots[l]; n++; l = "" }
     END { exit bad || n != 2 }' spill.s spill.s >spill.expected ||
  { echo "test precondition lost: spill() does not have two parameters whose location lists start with a base address entry and include an rsp-relative location"; exit 1; }
cat spill.expected
"$CV2PDB" spill.exe spill-cv.exe
"$CVDUMP" -s spill-cv.pdb >spill-cv.dump
awk 'FNR == NR { want[$1] = $0 " "; next }
     $2 == "S_GPROC32:" { inspill = $NF == "spill" }
     inspill && $2 == "S_REGREL32:" && ($NF in want) { got[$NF]++; split($3, r, "+"); if (r[1] != "rsp" || !index(want[$NF], " " strtonum("0x" substr(r[2], 1, 8)) " ")) bad = 1 }
     END { for (p in want) if (got[p] != 1) bad = 1; exit bad }' spill.expected spill-cv.dump
