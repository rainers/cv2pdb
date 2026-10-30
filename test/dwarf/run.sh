#!/bin/bash
#
# Runs every test script of a suite, or the named ones, and fails if any fails.
#
# usage: run.sh <suite> [name...]
#
# CV2PDB defaults to cv2pdb.exe in the x64 build output for BUILD_CONFIGURATION,
# which defaults to Release. CVDUMP defaults to cvdump.exe in the repository
# root. Set CV2PDB or CVDUMP to use another executable.
# TESTDIR is always set to the suite directory. Each test script copies its
# program sources from that directory.

set -euo pipefail

if [ $# -lt 1 ]; then
	echo "usage: $0 <suite> [name...]" >&2
	exit 2
fi
suite=$1
shift

dir=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$dir/../.." && pwd)
if [ ! -d "$dir/$suite" ]; then
	echo "unknown suite: $suite" >&2
	exit 2
fi

absolute() {
	case $1 in
	/* | [A-Za-z]:[\\/]*) echo "$1" ;;
	*) echo "$PWD/$1" ;;
	esac
}
CV2PDB=$(absolute "${CV2PDB:-$root/bin/${BUILD_CONFIGURATION:-Release}_x64/cv2pdb.exe}")
CVDUMP=$(absolute "${CVDUMP:-$root/cvdump.exe}")
TESTDIR=$dir/$suite
export CV2PDB CVDUMP TESTDIR
for var in CV2PDB CVDUMP; do
	if [ ! -f "${!var}" ]; then
		echo "$var not found: ${!var}" >&2
		exit 2
	fi
done

scripts=()
if [ $# -eq 0 ]; then
	scripts=("$dir/$suite"/*.sh)
else
	for name in "$@"; do
		script=$dir/$suite/${name%.sh}.sh
		if [ ! -f "$script" ]; then
			echo "unknown test: $suite/${name%.sh}" >&2
			exit 2
		fi
		scripts+=("$script")
	done
fi

failed=()
for script in "${scripts[@]}"; do
	name=$suite/$(basename "$script" .sh)
	scratch=$(mktemp -d)
	echo "::group::$name"
	echo "scratch directory: $scratch"
	ok=true
	# Each script runs as a new bash process. Bash ignores errexit in a sourced
	# file or a function whose exit status the caller tests, so a failing check
	# in a sourced script would not stop that script.
	(cd "$scratch" && bash --noprofile --norc -eo pipefail "$script") || ok=false
	echo "::endgroup::"
	if $ok; then
		rm -rf "$scratch"
	else
		echo "::error title=$name::test failed"
		failed+=("$name")
	fi
done

if [ ${#failed[@]} -ne 0 ]; then
	echo "failed tests: ${failed[*]}" >&2
	exit 1
fi
