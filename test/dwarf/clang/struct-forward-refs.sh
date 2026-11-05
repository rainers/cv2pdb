set -ex
cp "$TESTDIR/struct-forward-refs.c" fwdrefs.c

cat >fwdrefs.expected <<'EOF'
Anon.g@8 T_LONG
Anon.h@8 T_LONG
Anon.next@0 ptr8(Anon(16))
Anon.prev@0 ptr8(Anon(16))
CS.c@0 ptr8(const(CS(16)))
CS.v@8 T_LONG
MA.b@0 ptr8(MB(16))
MA.v@8 T_LONG
MB.a@0 ptr8(MA(16))
MB.w@8 T_LONG
Outer.inner@0 Outer::[anon](16)
Outer::[anon].up@0 ptr8(Outer(16))
Outer::[anon].v@8 T_LONG
PP.pp@0 ptr8(ptr8(PP(16)))
PP.v@8 T_LONG
Tdf.next@0 ptr8(Tdf(16))
Tdf.v@8 T_LONG
EOF

for v in 4 5; do
  clang --target=x86_64-w64-mingw32 -g -gdwarf-$v -O0 -S -o fwdrefs-d$v.s fwdrefs.c
  awk '/^\t\.section/ { info = $2 ~ /^\.debug_info/ }
       !info { next }
       /# Abbrev \[[0-9]+\] 0x[0-9a-f]+:0x[0-9a-f]+ DW_TAG_/ { split($6, o, ":"); die = strtonum(o[1]); tag[die] = $NF }
       /# DW_AT_name$/ { named[die] = 1 }
       /# DW_AT_type$/ { type[die] = $2 }
       END {
         for (d in type) {
           t = type[d]
           if (tag[d] == "DW_TAG_pointer_type" && tag[t] == "DW_TAG_typedef" && tag[type[t]] == "DW_TAG_structure_type") td_ok = 1
           if (tag[d] == "DW_TAG_pointer_type" && tag[t] == "DW_TAG_pointer_type" && tag[type[t]] == "DW_TAG_structure_type") pp_ok = 1
           if (tag[d] == "DW_TAG_pointer_type" && tag[t] == "DW_TAG_const_type" && tag[type[t]] == "DW_TAG_structure_type") pc_ok = 1
           if (tag[d] == "DW_TAG_member" && !named[d] && tag[t] == "DW_TAG_union_type") au_ok = 1
           if (tag[d] == "DW_TAG_member" && named[d] && tag[t] == "DW_TAG_structure_type" && !named[t]) as_ok = 1
         }
         if (!td_ok) print "test precondition lost: no DW_TAG_pointer_type to a DW_TAG_typedef of a DW_TAG_structure_type"
         if (!pp_ok) print "test precondition lost: no DW_TAG_pointer_type to a DW_TAG_pointer_type to a DW_TAG_structure_type"
         if (!pc_ok) print "test precondition lost: no DW_TAG_pointer_type to a DW_TAG_const_type of a DW_TAG_structure_type"
         if (!au_ok) print "test precondition lost: no unnamed DW_TAG_member whose type is a DW_TAG_union_type"
         if (!as_ok) print "test precondition lost: no named DW_TAG_member whose type is an unnamed DW_TAG_structure_type"
         exit !(td_ok && pp_ok && pc_ok && au_ok && as_ok)
       }' fwdrefs-d$v.s
  clang --target=x86_64-w64-mingw32 -fuse-ld=lld -g -gdwarf-$v -O0 -o fwdrefs-d$v.exe fwdrefs.c
  "$CV2PDB" fwdrefs-d$v.exe fwdrefs-cv-d$v.exe
  "$CVDUMP" -t fwdrefs-cv-d$v.pdb >fwdrefs-cv-d$v.types
  if grep -n T_NOTTRANS fwdrefs-cv-d$v.types; then
    echo "T_NOTTRANS in the type records of fwdrefs-cv-d$v.pdb"
    exit 1
  fi
  # The awk command below prints each member of the structures of fwdrefs.c with its type,
  # written as the chain of LF_MODIFIER and LF_POINTER records down to a basic type or a structure.
  awk 'function render(t,  k) {
         if (t ~ /^T_/) return substr(t, 1, index(t, "(") - 1)
         k = tolower(t)
         if (leaf[k] == "LF_MODIFIER") return attr[k] == "" ? render(base[k]) : attr[k] "(" render(base[k]) ")"
         if (leaf[k] == "LF_POINTER") return "ptr" size[k] "(" render(base[k]) ")"
         if (leaf[k] == "LF_STRUCTURE") return name[k] "(" size[k] ")"
         return leaf[k] "?" t
       }
       /^0x[0-9a-f]+ : / { id = $1; leaf[id] = $NF; next }
       /modifies type/ { a = $0; sub(/^[ \t]*/, "", a); sub(/ *modifies type.*/, "", a); gsub(/,/, "", a); sub(/ $/, "", a); attr[id] = a; base[id] = $NF; next }
       /Pointer \(/ { size[id] = $NF; next }
       /Element type :/ { base[id] = $NF; next }
       /field list type/ { fl[id] = tolower($NF); sub(/,$/, "", fl[id]); next }
       /class name = / { split($0, f, ", "); sub(/.*= /, "", f[1]); sub(/.*= /, "", f[2]); size[id] = f[1]; name[id] = f[2]; sub(/\[anon_[0-9a-f]+\]/, "[anon]", name[id]); next }
       /LF_MEMBER/ { split($0, f, ", "); sub(/.*= /, "", f[3]); sub(/.*= /, "", f[4]); mtype = f[3]; moff = f[4]; next }
       /member name = / { m = $NF; gsub(/\047/, "", m); members[id] = members[id] m "@" moff " " mtype "\n"; next }
       END {
         for (k in name) {
           if (name[k] !~ /^(Tdf|PP|CS|Anon|MA|MB|Outer|Outer::\[anon\])$/ || !(fl[k] in members)) continue
           n = split(members[fl[k]], ml, "\n")
           for (i = 1; i < n; i++) { split(ml[i], p, " "); print name[k] "." p[1], render(p[2]) }
         }
       }' fwdrefs-cv-d$v.types | LC_ALL=C sort >fwdrefs-cv-d$v.actual
  diff fwdrefs.expected fwdrefs-cv-d$v.actual
done
