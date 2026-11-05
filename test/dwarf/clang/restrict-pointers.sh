set -ex
cp "$TESTDIR/restrict-pointers.c" restrict.c

cat >restrict.expected <<'EOF'
g_crp restrict ptr8(const(T_RCHAR))
g_rcp const(restrict ptr8(T_RCHAR))
g_rcvp const volatile(restrict ptr8(T_RCHAR))
g_rp restrict ptr8(T_RCHAR)
g_rsp restrict ptr8(S(4))
g_rtp ptr8(T_RCHAR)
EOF

for v in 4 5; do
  clang --target=x86_64-w64-mingw32 -g -gdwarf-$v -O0 -S -o restrict-d$v.s restrict.c
  awk '/^\t\.section/ { info = $2 ~ /^\.debug_info/ }
       !info { next }
       /# Abbrev \[[0-9]+\] 0x[0-9a-f]+:0x[0-9a-f]+ DW_TAG_/ { split($6, o, ":"); die = strtonum(o[1]); tag[die] = $NF }
       /# DW_AT_type$/ { type[die] = $2 }
       END {
         for (d in type) {
           if (tag[d] == "DW_TAG_restrict_type" && tag[type[d]] == "DW_TAG_pointer_type") rp_ok = 1
           if (tag[d] == "DW_TAG_const_type" && tag[type[d]] == "DW_TAG_restrict_type") cr_ok = 1
           if (tag[d] == "DW_TAG_restrict_type" && tag[type[d]] == "DW_TAG_typedef") rt_ok = 1
         }
         if (!rp_ok) print "test precondition lost: no DW_TAG_restrict_type over a DW_TAG_pointer_type"
         if (!cr_ok) print "test precondition lost: no DW_TAG_const_type over a DW_TAG_restrict_type"
         if (!rt_ok) print "test precondition lost: no DW_TAG_restrict_type over a DW_TAG_typedef"
         exit !(rp_ok && cr_ok && rt_ok)
       }' restrict-d$v.s
  clang --target=x86_64-w64-mingw32 -fuse-ld=lld -g -gdwarf-$v -O0 -o restrict-d$v.exe restrict.c
  "$CV2PDB" restrict-d$v.exe restrict-cv-d$v.exe
  "$CVDUMP" -t restrict-cv-d$v.pdb >restrict-cv-d$v.types
  "$CVDUMP" -g restrict-cv-d$v.pdb >restrict-cv-d$v.globals
  # The awk command below prints each restrict global of restrict.c with its type, written as
  # the chain of LF_MODIFIER and LF_POINTER records down to a basic type or a structure.
  awk 'function render(t,  k) {
         if (t ~ /^T_/) return substr(t, 1, index(t, "(") - 1)
         k = tolower(t)
         if (leaf[k] == "LF_MODIFIER") return attr[k] == "" ? render(base[k]) : attr[k] "(" render(base[k]) ")"
         if (leaf[k] == "LF_POINTER") return (attr[k] == "" ? "" : attr[k] " ") "ptr" size[k] "(" render(base[k]) ")"
         if (leaf[k] == "LF_STRUCTURE") return name[k] "(" size[k] ")"
         return leaf[k] "?" t
       }
       FNR == NR && /^0x[0-9a-f]+ : / { id = $1; leaf[id] = $NF; next }
       FNR == NR && /modifies type/ { a = $0; sub(/^[ \t]*/, "", a); sub(/ *modifies type.*/, "", a); gsub(/,/, "", a); sub(/ $/, "", a); attr[id] = a; base[id] = $NF; next }
       FNR == NR && /Pointer \(/ { a = $0; sub(/^[ \t]*/, "", a); sub(/ *Pointer \(.*/, "", a); sub(/^__/, "", a); attr[id] = a; size[id] = $NF; next }
       FNR == NR && /Element type :/ { base[id] = $NF; next }
       FNR == NR && /class name = / { split($0, f, ", "); sub(/.*= /, "", f[1]); sub(/.*= /, "", f[2]); size[id] = f[1]; name[id] = f[2]; next }
       FNR == NR { next }
       $1 == "S_GDATA32:" && $NF ~ /^g_(rp|crp|rcp|rcvp|rsp|rtp)$/ { t = $(NF - 1); sub(/,$/, "", t); print $NF, render(t) }
       ' restrict-cv-d$v.types restrict-cv-d$v.globals | LC_ALL=C sort >restrict-cv-d$v.actual
  diff restrict.expected restrict-cv-d$v.actual
done
