set -ex
cp "$TESTDIR/function-pointers.c" fp.c

cat >fp.expected <<'EOF'
g_cfp const(ptr8(proc T_VOID()))
g_fp ptr8(proc T_LONG(T_LONG))
g_h ptr8(proc T_LONG(T_LONG))
g_ops ops(16)
g_var ptr8(proc T_LONG(ptr8(const(T_RCHAR)),...))
ops.done@8 ptr8(proc T_VOID())
ops.op@0 ptr8(proc T_LONG(T_LONG,T_REAL64))
EOF

for v in 4 5; do
  clang --target=x86_64-w64-mingw32 -g -gdwarf-$v -O0 -S -o fp-d$v.s fp.c
  awk '/^\t\.section/ { info = $2 ~ /^\.debug_info/ }
       !info { next }
       /# Abbrev \[[0-9]+\] 0x[0-9a-f]+:0x[0-9a-f]+ DW_TAG_/ {
         split($6, o, ":"); die = strtonum(o[1]); tag[die] = $NF
         if ($NF == "DW_TAG_unspecified_parameters" && owner == "DW_TAG_subroutine_type") va_ok = 1
         if ($NF != "DW_TAG_formal_parameter" && $NF != "DW_TAG_unspecified_parameters") owner = $NF
       }
       /# DW_AT_type$/ { type[die] = $2 }
       END {
         for (d in type) {
           if (tag[d] == "DW_TAG_pointer_type" && tag[type[d]] == "DW_TAG_subroutine_type") ptr_ok = 1
           if (tag[d] == "DW_TAG_typedef" && tag[type[d]] == "DW_TAG_subroutine_type") td_ok = 1
         }
         if (!ptr_ok) print "test precondition lost: no DW_TAG_pointer_type to a DW_TAG_subroutine_type"
         if (!td_ok) print "test precondition lost: no DW_TAG_typedef of a DW_TAG_subroutine_type"
         if (!va_ok) print "test precondition lost: no DW_TAG_subroutine_type with DW_TAG_unspecified_parameters"
         exit !(ptr_ok && td_ok && va_ok)
       }' fp-d$v.s
  clang --target=x86_64-w64-mingw32 -fuse-ld=lld -g -gdwarf-$v -O0 -o fp-d$v.exe fp.c
  "$CV2PDB" fp-d$v.exe fp-cv-d$v.exe
  "$CVDUMP" -t fp-cv-d$v.pdb >fp-cv-d$v.types
  "$CVDUMP" -g fp-cv-d$v.pdb >fp-cv-d$v.globals
  # The awk command below prints each global of fp.c and each member of struct ops with its type,
  # written as the chain of type records down to a basic type or a structure. A procedure is
  # written as its return type followed by its argument types, with ... for a T_NOTYPE argument.
  awk 'function render(t,  k, n, i, a, s) {
         if (t ~ /^T_/) return substr(t, 1, index(t, "(") - 1)
         k = tolower(t)
         if (leaf[k] == "LF_MODIFIER") return attr[k] == "" ? render(base[k]) : attr[k] "(" render(base[k]) ")"
         if (leaf[k] == "LF_POINTER") return "ptr" size[k] "(" render(base[k]) ")"
         if (leaf[k] == "LF_STRUCTURE") return name[k] "(" size[k] ")"
         if (leaf[k] == "LF_PROCEDURE") {
           n = split(args[tolower(arglist[k])], a, " ")
           for (i = 1; i <= n; i++) s = s (i > 1 ? "," : "") (a[i] == "T_NOTYPE(0000)" ? "..." : render(a[i]))
           return "proc " render(rvtype[k]) "(" s ")"
         }
         return leaf[k] "?" t
       }
       FNR == NR && /^0x[0-9a-f]+ : / { id = $1; leaf[id] = $9; next }
       FNR == NR && /modifies type/ { a = $0; sub(/^[ \t]*/, "", a); sub(/ *modifies type.*/, "", a); gsub(/,/, "", a); sub(/ $/, "", a); attr[id] = a; base[id] = $NF; next }
       FNR == NR && /Pointer \(/ { size[id] = $NF; next }
       FNR == NR && /Element type :/ { base[id] = $NF; next }
       FNR == NR && /Return type = / { rvtype[id] = $4; sub(/,$/, "", rvtype[id]); next }
       FNR == NR && /Arg list type = / { arglist[id] = $NF; next }
       FNR == NR && leaf[id] == "LF_ARGLIST" && /list\[[0-9]+\] = / { args[id] = args[id] " " $NF; next }
       FNR == NR && /field list type/ { fl[id] = tolower($NF); sub(/,$/, "", fl[id]); next }
       FNR == NR && /class name = / { split($0, f, ", "); sub(/.*= /, "", f[1]); sub(/.*= /, "", f[2]); size[id] = f[1]; name[id] = f[2]; next }
       FNR == NR && /LF_MEMBER/ { split($0, f, ", "); sub(/.*= /, "", f[3]); sub(/.*= /, "", f[4]); mtype = f[3]; moff = f[4]; next }
       FNR == NR && /member name = / { m = $NF; gsub(/\047/, "", m); members[id] = members[id] m "@" moff " " mtype "\n"; next }
       FNR == NR { next }
       $1 == "S_GDATA32:" && $NF ~ /^g_/ { t = $(NF - 1); sub(/,$/, "", t); print $NF, render(t) }
       END {
         for (k in name) {
           if (name[k] != "ops" || !(fl[k] in members)) continue
           n = split(members[fl[k]], ml, "\n")
           for (i = 1; i < n; i++) { split(ml[i], p, " "); print "ops." p[1], render(p[2]) }
         }
       }' fp-cv-d$v.types fp-cv-d$v.globals | LC_ALL=C sort >fp-cv-d$v.actual
  diff fp.expected fp-cv-d$v.actual
done
