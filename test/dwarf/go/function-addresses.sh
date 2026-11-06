set -ex
cp "$TESTDIR/dwarf5-program.go" main.go

go version
CGO_ENABLED=0 go build -ldflags=-compressdwarf=false -o go-dwarf.exe main.go
go tool nm go-dwarf.exe >go-dwarf.nm
"$CV2PDB" go-dwarf.exe go-cv.exe
"$CVDUMP" -s go-cv.pdb >go-cv.dump

# Procedure records have the function addresses that go tool nm reports.
text=$(awk '$3 == "runtime.text" { print $1 }' go-dwarf.nm)
for f in main.call main.main 'main.(*Value).Get'; do
  addr=$(awk -v n="$f" '$2 == "T" && $3 == n { print $1 }' go-dwarf.nm)
  off=$(printf '%08X' $((0x$addr - 0x$text)))
  awk -v a="[0001:$off]," -v n=", ${f//./@}" '$2 == "S_GPROC32:" && $3 == a && substr($0, length($0) - length(n) + 1) == n { found=1 } END {exit !found; }' go-cv.dump
done
