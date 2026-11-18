set -ex
cp "$TESTDIR/array-lengths-above-4gb.c" big.c

for v in 4 5; do
  clang --target=x86_64-w64-mingw32 -g -gdwarf-$v -O0 -S -o big-d$v.s big.c
  awk -v out=big-d$v.dwarf '
       function hex(s,   n, i) {
         n = 0
         for (i = 3; i <= length(s); i++)
           n = n * 16 + index("0123456789abcdef", substr(tolower(s), i, 1)) - 1
         return n
       }
       /^\t\.section/ { info = $2 ~ /^\.debug_info/ }
       !info { next }
       / Abbrev \[[0-9]+\] 0x[0-9a-f]+:0x[0-9a-f]+ DW_TAG_/ {
         for (i = 1; i <= NF; i++)
           if ($i ~ /^0x[0-9a-f]+:0x/) { split($i, p, ":"); die = hex(p[1]) }
         if ($NF == "DW_TAG_array_type") arrays[++n] = die
         if ($NF == "DW_TAG_subrange_type") dims[arrays[n], ++ndims[arrays[n]]] = die
         next
       }
       $NF == "DW_AT_type" { type[die] = $2 }
       $NF == "DW_AT_byte_size" { size[die] = $2 }
       $NF == "DW_AT_count" { count[die] = $2 }
       $NF == "DW_AT_upper_bound" { upper = 1 }
       END {
         for (i = 1; i <= n; i++) {
           t = type[arrays[i]]
           while (!(t in size) && (t in type))
             t = type[t]
           s = size[t]
           for (d = ndims[arrays[i]]; d >= 1; d--) {
             s *= count[dims[arrays[i], d]]
             printf "%.0f\n", s > out
             if (s > 4294967295) large = 1
           }
         }
         if (!large) print "test precondition lost: no array type in " FILENAME " is 4 GB or larger"
         if (upper) print "test precondition lost: an array dimension in " FILENAME " is described by DW_AT_upper_bound"
         exit !(large && !upper)
       }' big-d$v.s
  sort big-d$v.dwarf >big-d$v.expected
  clang --target=x86_64-w64-mingw32 -fuse-ld=lld -g -gdwarf-$v -O0 -o big-d$v.exe big.c
  "$CV2PDB" big-d$v.exe big-cv-d$v.exe
  "$CVDUMP" -t big-cv-d$v.pdb >big-cv-d$v.types
  "$CVDUMP" -g big-cv-d$v.pdb >big-cv-d$v.globals
  awk 'FNR == NR && /^0x[0-9a-f]+ : / { t = tolower($1); kind = $NF; next }
       FNR == NR && kind == "LF_MODIFIER" && $1 == "modifies" { modified[t] = tolower($3); next }
       FNR == NR && kind == "LF_ARRAY" && $1 == "Element" { elem[t] = tolower($4); next }
       FNR == NR && kind == "LF_ARRAY" && $1 == "length" { len[t] = $3 ~ /^\(LF_/ ? $4 : $3; next }
       FNR == NR { next }
       $1 == "S_UDT:" && ($NF == "wide_t" || $NF == "grid_t") {
         t = tolower($2)
         sub(/,$/, "", t)
         while (t in modified)
           t = modified[t]
         for (; t in len; t = elem[t])
           print len[t]
       }' big-cv-d$v.types big-cv-d$v.globals >big-cv-d$v.lengths
  sort big-cv-d$v.lengths >big-d$v.actual
  diff big-d$v.expected big-d$v.actual
done
