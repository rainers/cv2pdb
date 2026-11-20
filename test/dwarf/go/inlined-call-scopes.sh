set -ex
cp "$TESTDIR/inlined-call-scopes.go" main.go
mkdir -p gaps
cp "$TESTDIR/inlined-call-scopes-gaps.go" gaps/main.go

go version
CGO_ENABLED=0 go build -ldflags=-compressdwarf=false -o calls.exe main.go
go run gaps/main.go calls.exe main.Walk step >gaps.txt
test -s gaps.txt || { echo "test precondition lost: no inlined call in main.Walk lies between two ranges of a lexical block that declares 'step'"; exit 1; }
"$CV2PDB" calls.exe calls-cv.exe
"$CVDUMP" -s calls-cv.pdb >calls-cv.dump

# The S_BLOCK32 scope of the inlined call of twice() is nested in an S_BLOCK32 scope where `step` has
# a range over the inlined call, so a debugger shows `step` while the PC is in the inlined code.
awk '$2 == "S_GPROC32:" { fn = $NF; d = 0; next }
     fn != "main@Walk" { next }
     $2 == "S_BLOCK32:" { split($3, a, /[:\]]/); d++; lo[d] = strtonum("0x" a[2]); hi[d] = lo[d] + strtonum("0x" substr($5, 1, 8)); n[d] = 0; next }
     $2 == "S_END" { if (!d) fn = ""; d--; next }
     $2 == "S_LOCAL:" {
       var = $NF
       if (var == "value" && d) {
         calls++
         for (k = 1; k < d; k++) for (i = 1; i <= n[k]; i++) if (s[k, i] <= lo[d] && hi[d] <= e[k, i]) nested++
         if (!nested) printf "the S_BLOCK32 of the inlined call at %x is not nested in a scope where `step` has a range over it\n", lo[d] >"/dev/stderr"
       }
       next
     }
     var == "step" && $1 == "Range:" && d { split($2, r, /[:\]]/); split($4, t, /[:\]]/); n[d]++; s[d, n[d]] = strtonum("0x" r[2]); e[d, n[d]] = strtonum("0x" t[2]) }
     $2 ~ /^S_/ && $2 !~ /^S_DEFRANGE_/ { var = "" }
     END {
       if (calls != 1) printf "the parameter `value` of the inlined call of twice() has %d S_LOCAL records in S_BLOCK32 scopes of main.Walk, not one\n", calls >"/dev/stderr"
       exit !(calls == 1 && nested)
     }' calls-cv.dump
