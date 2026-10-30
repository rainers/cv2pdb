set -ex
cp "$TESTDIR/string-parameters.go" main.go

go version
mkdir ld
CGO_ENABLED=1 go build -ldflags="-compressdwarf=false -linkmode=external -tmpdir=$(pwd -W)/ld" -o strparam.exe main.go
test -f ld/go.o || { echo "test precondition lost: the external link left no go.o behind"; exit 1; }
llvm-objdump -d --debug-vars=ascii --no-show-raw-insn --disassemble-symbols=main.Count ld/go.o >strparam.vars
grep -E '[/|]- s = .*DW_OP_piece' strparam.vars || { echo "test precondition lost: the DWARF does not describe parameter 's' in pieces"; exit 1; }
grep -E '[/|]- s = .*(DW_OP_call_frame_cfa|DW_OP_fbreg)' strparam.vars || { echo "test precondition lost: the DWARF does not place parameter 's' on the stack"; exit 1; }
"$CV2PDB" strparam.exe strparam-cv.exe
"$CVDUMP" -s strparam-cv.pdb >strparam-cv.dump

# The 16-byte string `s` gets one S_LOCAL record without the parameter flag. The pointer of `s` is
# in rax and the length of `s` is in rbx from the start of main.Count, up to the address where a
# whole-variable record places `s` on the stack.
awk '$2 == "S_GPROC32:" { inp = $NF == "main@Count"; split($3, a, /[:\]]/); lo = a[2]; var = ""; next }
     !inp { next }
     $2 == "S_LOCAL:" { var = $NF; if (var == "s") { print; n++; kind = $3 } next }
     var == "s" && $2 ~ /^S_DEFRANGE_/ { print; rec = $2 == "S_DEFRANGE_SUBFIELD_REGISTER:" ? $5 $6 : $2 == "S_DEFRANGE_REGISTER_REL:" && $3 == "[rsp" && NF == 6 ? "whole" : "other"; next }
     var == "s" && $1 == "Range:" {
       print; split($2, r, /[:\]]/); split($4, e, /[:\]]/)
       if (r[2] == lo && rec == "0000:rax") ptr = e[2]
       if (r[2] == lo && rec == "0008:rbx") len = e[2]
       if (rec == "whole") whole[r[2]] = 1
     }
     $2 ~ /^S_/ { var = "" }
     END { exit !(n == 1 && kind == "Local:" && ptr != "" && ptr == len && (ptr in whole)) }' strparam-cv.dump
