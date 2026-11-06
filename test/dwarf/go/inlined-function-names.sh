set -ex
cp "$TESTDIR/dwarf5-program.go" main.go

go version
CGO_ENABLED=0 go build -ldflags=-compressdwarf=false -o go-dwarf.exe main.go
"$CV2PDB" go-dwarf.exe go-cv.exe
"$CVDUMP" -s go-cv.pdb >go-cv.dump

# Procedure records of inlined functions keep their Go names, not anon_ names.
grep 'S_GPROC32: .*, main@Twice$' go-cv.dump
if grep 'S_GPROC32: .*\[anon_' go-cv.dump; then exit 1; fi
