# Prints the ranges of the variables named `var` in the output of llvm-objdump --debug-vars=ascii,
# with its tabs expanded, as offsets from the start of the disassembled function. Each line is
# "lo hi loc", or "lo hi lit <n>" for a DW_OP_lit<n> location, and hi is "end" for a range that
# ends after the last instruction. llvm-objdump draws the start of a range on the instruction
# before the start address, and the end of a range on the instruction before the end address.
# llvm-objdump draws "|-" for a range that starts at the first instruction.
# llvm-objdump draws the ranges in columns that line up only with the tabs of the instructions
# expanded.
$2 ~ /^<.*>:$/ { base = strtonum("0x" $1); next }
/^ *[0-9a-f]+:/ {
  a = strtonum("0x" substr($1, 1, length($1) - 1)) - base
  for (c in pending) { lo[c] = a; delete pending[c] }
  for (c in closing) { print lo[c], a, kind[c]; delete closing[c]; delete kind[c] }
  for (c in first) { lo[c] = a; delete first[c] }
  for (c in kind) { ch = substr($0, c, 1); if (ch == "^") pending[c] = 1; else if (ch == "v") closing[c] = 1 }
  next
}
(p = index($0, "- " var " = ")) {
  c = p - 1; loc = substr($0, p + length(var) + 5)
  kind[c] = match(loc, /^<unknown op DW_OP_lit[0-9]+ /) ? "lit " substr(loc, 22, RLENGTH - 22) : "loc"
  if (substr($0, c, 1) == "|") first[c] = 1
}
END { for (c in closing) print lo[c], "end", kind[c] }
