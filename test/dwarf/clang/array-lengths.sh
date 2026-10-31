set -ex
cp "$TESTDIR/array-lengths.c" array.c

printf '%s\n' 'g_chars 8' 'g_matrix 60 20' 'member_items 12' 'member_zero 0' >array.expected
for v in 4 5; do
  clang --target=x86_64-w64-mingw32 -g -gdwarf-$v -O0 -S -o array-d$v.s array.c
  awk '/^\t\.section/ { info = $2 ~ /^\.debug_info/ }
       info && $NF == "DW_AT_count" { count = 1 }
       info && $NF == "DW_AT_upper_bound" { upper = 1 }
       END {
         if (!count) print "test precondition lost: no array dimension is described by DW_AT_count"
         if (upper) print "test precondition lost: an array dimension is described by DW_AT_upper_bound"
         exit !(count && !upper)
       }' array-d$v.s
  clang --target=x86_64-w64-mingw32 -fuse-ld=lld -g -gdwarf-$v -O0 -o array-d$v.exe array.c
  "$CV2PDB" array-d$v.exe array-cv-d$v.exe
  "$CVDUMP" -t array-cv-d$v.pdb >array-cv-d$v.types
  "$CVDUMP" -g array-cv-d$v.pdb >array-cv-d$v.globals
  awk 'FNR == NR && /^0x[0-9a-f]+ : / { t = tolower($1); kind = $NF; next }
       FNR == NR && kind == "LF_ARRAY" && $1 == "Element" { elem[t] = tolower($4); next }
       FNR == NR && kind == "LF_ARRAY" && $1 == "length" { len[t] = $3; next }
       FNR == NR && kind == "LF_FIELDLIST" && $3 == "LF_MEMBER," { mtype = tolower($7); sub(/,$/, "", mtype); next }
       FNR == NR && kind == "LF_FIELDLIST" && $1 == "member" { name = $4; gsub(/\047/, "", name); member[name] = mtype; next }
       FNR == NR { next }
       $1 == "S_GDATA32:" { t = tolower($4); sub(/,$/, "", t); global[$NF] = t }
       END {
         print "g_chars", len[global["g_chars"]]
         print "g_matrix", len[global["g_matrix"]], len[elem[global["g_matrix"]]]
         print "member_items", len[member["member_items"]]
         print "member_zero", len[member["member_zero"]]
       }' array-cv-d$v.types array-cv-d$v.globals >array-d$v.actual
  diff array.expected array-d$v.actual
done
