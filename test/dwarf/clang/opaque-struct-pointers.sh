set -ex
cp "$TESTDIR/opaque-struct-pointers.c" opaque.c

cat >opaque.expected <<'EOF'
HasOpaque.op@0 ptr8(fwd(Opaque))
HasOpaque.opt@8 ptr8(fwd(OpaqueT))
g_op ptr8(fwd(Opaque))
g_opt ptr8(fwd(OpaqueT))
g_opu ptr8(fwd(OpaqueU))
EOF

for v in 4 5; do
  clang --target=x86_64-w64-mingw32 -g -gdwarf-$v -O0 -S -o opaque-d$v.s opaque.c
  awk '/^\t\.section/ { abbrev = $2 ~ /^\.debug_abbrev/; info = $2 ~ /^\.debug_info/ }
       abbrev && /# Abbreviation Code$/ { code = $2 }
       abbrev && /# DW_AT_declaration$/ { decl[code] = 1 }
       !info { next }
       /# Abbrev \[[0-9]+\] 0x[0-9a-f]+:0x[0-9a-f]+ DW_TAG_/ {
         split($6, o, ":"); die = strtonum(o[1]); tag[die] = $NF
         c = $5; gsub(/[][]/, "", c); isdecl[die] = (c in decl)
       }
       /# DW_AT_type$/ { type[die] = $2 }
       function declared(d, t) { return tag[d] == t && isdecl[d] }
       END {
         for (d in type) {
           p = type[d]
           if (tag[d] == "DW_TAG_pointer_type" && declared(p, "DW_TAG_structure_type")) direct_ok = 1
           if (tag[d] == "DW_TAG_pointer_type" && tag[p] == "DW_TAG_typedef" && declared(type[p], "DW_TAG_structure_type")) typedef_ok = 1
           if (tag[d] == "DW_TAG_pointer_type" && declared(p, "DW_TAG_union_type")) union_ok = 1
           if (tag[d] == "DW_TAG_member" && tag[p] == "DW_TAG_pointer_type" && declared(type[p], "DW_TAG_structure_type")) member_ok = 1
         }
         if (!direct_ok) print "test precondition lost: no DW_TAG_pointer_type to a declared DW_TAG_structure_type"
         if (!typedef_ok) print "test precondition lost: no DW_TAG_pointer_type to a DW_TAG_typedef of a declared DW_TAG_structure_type"
         if (!union_ok) print "test precondition lost: no DW_TAG_pointer_type to a declared DW_TAG_union_type"
         if (!member_ok) print "test precondition lost: no DW_TAG_member whose type is a DW_TAG_pointer_type to a declared DW_TAG_structure_type"
         exit !(direct_ok && typedef_ok && union_ok && member_ok)
       }' opaque-d$v.s
  clang --target=x86_64-w64-mingw32 -fuse-ld=lld -g -gdwarf-$v -O0 -o opaque-d$v.exe opaque.c
  "$CV2PDB" opaque-d$v.exe opaque-cv-d$v.exe
  "$CVDUMP" -t opaque-cv-d$v.pdb >opaque-cv-d$v.types
  "$CVDUMP" -g opaque-cv-d$v.pdb >opaque-cv-d$v.globals
  # The awk command below prints each pointer global of opaque.c and each pointer member of HasOpaque with
  # its type, written as the chain of LF_MODIFIER and LF_POINTER records down to a basic type or a
  # structure. A forward reference to a structure is written as fwd(name).
  awk 'function render(t,  k) {
         if (t ~ /^T_/) return substr(t, 1, index(t, "(") - 1)
         k = tolower(t)
         if (leaf[k] == "LF_MODIFIER") return attr[k] == "" ? render(base[k]) : attr[k] "(" render(base[k]) ")"
         if (leaf[k] == "LF_POINTER") return "ptr" size[k] "(" render(base[k]) ")"
         if (leaf[k] == "LF_STRUCTURE") return fwd[k] ? "fwd(" name[k] ")" : name[k] "(" size[k] ")"
         return leaf[k] "?" t
       }
       FNR == NR && /^0x[0-9a-f]+ : / { id = $1; leaf[id] = $NF; next }
       FNR == NR && /modifies type/ { a = $0; sub(/^[ \t]*/, "", a); sub(/ *modifies type.*/, "", a); gsub(/,/, "", a); sub(/ $/, "", a); attr[id] = a; base[id] = $NF; next }
       FNR == NR && /Pointer \(/ { size[id] = $NF; next }
       FNR == NR && /Element type :/ { base[id] = $NF; next }
       FNR == NR && /field list type/ { fwd[id] = /FORWARD REF/; l = $0; sub(/.*field list type /, "", l); sub(/,.*/, "", l); fl[id] = tolower(l); next }
       FNR == NR && /class name = / { split($0, f, ", "); sub(/.*= /, "", f[1]); sub(/.*= /, "", f[2]); size[id] = f[1]; name[id] = f[2]; next }
       FNR == NR && /LF_MEMBER/ { split($0, f, ", "); sub(/.*= /, "", f[3]); sub(/.*= /, "", f[4]); mtype = f[3]; moff = f[4]; next }
       FNR == NR && /member name = / { m = $NF; gsub(/\047/, "", m); members[id] = members[id] m "@" moff " " mtype "\n"; next }
       FNR == NR { next }
       $1 == "S_GDATA32:" && $NF ~ /^g_op/ { t = $(NF - 1); sub(/,$/, "", t); print $NF, render(t) }
       END {
         for (k in name) {
           if (name[k] != "HasOpaque" || !(fl[k] in members)) continue
           n = split(members[fl[k]], ml, "\n")
           for (i = 1; i < n; i++) { split(ml[i], p, " "); if (p[1] ~ /^op/) print "HasOpaque." p[1], render(p[2]) }
         }
       }' opaque-cv-d$v.types opaque-cv-d$v.globals | LC_ALL=C sort >opaque-cv-d$v.actual
  diff opaque.expected opaque-cv-d$v.actual
done
