set -ex
cp "$TESTDIR/volatile-types.c" volatile.c

cat >volatile.expected <<'EOF'
S.m@0 volatile(T_LONG)
S.n@4 T_LONG
g_c const(T_LONG)
g_cv const volatile(T_LONG)
g_pv ptr8(volatile(T_LONG))
g_s S(8)
g_seed volatile(T_LONG)
g_vd volatile(T_REAL64)
g_vll volatile(T_QUAD)
g_vp volatile(ptr8(T_LONG))
g_vs volatile(S(8))
EOF

for v in 4 5; do
  clang --target=x86_64-w64-mingw32 -g -gdwarf-$v -O0 -S -o volatile-d$v.s volatile.c
  awk '/^\t\.section/ { info = $2 ~ /^\.debug_info/ }
       !info { next }
       /# Abbrev \[[0-9]+\] 0x[0-9a-f]+:0x[0-9a-f]+ DW_TAG_/ { split($6, o, ":"); die = strtonum(o[1]); tag[die] = $NF }
       /# DW_AT_type$/ { type[die] = $2 }
       END {
         for (d in type) {
           if (tag[d] == "DW_TAG_const_type" && tag[type[d]] == "DW_TAG_volatile_type") cv_ok = 1
           if (tag[d] == "DW_TAG_volatile_type" && tag[type[d]] == "DW_TAG_pointer_type") vp_ok = 1
           if (tag[d] == "DW_TAG_pointer_type" && tag[type[d]] == "DW_TAG_volatile_type") pv_ok = 1
         }
         if (!cv_ok) print "test precondition lost: no DW_TAG_const_type over a DW_TAG_volatile_type"
         if (!vp_ok) print "test precondition lost: no DW_TAG_volatile_type over a DW_TAG_pointer_type"
         if (!pv_ok) print "test precondition lost: no DW_TAG_pointer_type to a DW_TAG_volatile_type"
         exit !(cv_ok && vp_ok && pv_ok)
       }' volatile-d$v.s
  clang --target=x86_64-w64-mingw32 -fuse-ld=lld -g -gdwarf-$v -O0 -o volatile-d$v.exe volatile.c
  "$CV2PDB" volatile-d$v.exe volatile-cv-d$v.exe
  "$CVDUMP" -t volatile-cv-d$v.pdb >volatile-cv-d$v.types
  "$CVDUMP" -g volatile-cv-d$v.pdb >volatile-cv-d$v.globals
  # The awk command below prints each global of volatile.c and each member of S with its type,
  # written as the chain of LF_MODIFIER and LF_POINTER records down to a basic type or a structure.
  awk 'function render(t,  k) {
         if (t ~ /^T_/) return substr(t, 1, index(t, "(") - 1)
         k = tolower(t)
         if (leaf[k] == "LF_MODIFIER") return attr[k] == "" ? render(base[k]) : attr[k] "(" render(base[k]) ")"
         if (leaf[k] == "LF_POINTER") return "ptr" size[k] "(" render(base[k]) ")"
         if (leaf[k] == "LF_STRUCTURE") return name[k] "(" size[k] ")"
         return leaf[k] "?" t
       }
       FNR == NR && /^0x[0-9a-f]+ : / { id = $1; leaf[id] = $NF; next }
       FNR == NR && /modifies type/ { a = $0; sub(/^[ \t]*/, "", a); sub(/ *modifies type.*/, "", a); gsub(/,/, "", a); sub(/ $/, "", a); attr[id] = a; base[id] = $NF; next }
       FNR == NR && /Pointer \(/ { size[id] = $NF; next }
       FNR == NR && /Element type :/ { base[id] = $NF; next }
       FNR == NR && /field list type/ { fl[id] = tolower($NF); sub(/,$/, "", fl[id]); next }
       FNR == NR && /class name = / { split($0, f, ", "); sub(/.*= /, "", f[1]); sub(/.*= /, "", f[2]); size[id] = f[1]; name[id] = f[2]; next }
       FNR == NR && /LF_MEMBER/ { split($0, f, ", "); sub(/.*= /, "", f[3]); sub(/.*= /, "", f[4]); mtype = f[3]; moff = f[4]; next }
       FNR == NR && /member name = / { m = $NF; gsub(/\047/, "", m); members[id] = members[id] m "@" moff " " mtype "\n"; next }
       FNR == NR { next }
       $1 == "S_GDATA32:" && $NF ~ /^g_/ { t = $(NF - 1); sub(/,$/, "", t); print $NF, render(t) }
       END {
         for (k in name) {
           if (name[k] != "S" || !(fl[k] in members)) continue
           n = split(members[fl[k]], ml, "\n")
           for (i = 1; i < n; i++) { split(ml[i], p, " "); print "S." p[1], render(p[2]) }
         }
       }' volatile-cv-d$v.types volatile-cv-d$v.globals | LC_ALL=C sort >volatile-cv-d$v.actual
  diff volatile.expected volatile-cv-d$v.actual
done
