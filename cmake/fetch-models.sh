#!/bin/sh
# Provisions the inference models for one platform profile.
#
# Kerfur boots offline, so nothing is downloaded at run time. This runs once,
# verifies every file against the sha256 in assets/models.json, and leaves a
# directory ModelCatalog can resolve.
#
# Usage: fetch-models.sh <profile> <destination> [cache]
#   profile      desktop, pi or wasm; selects which models the manifest lists
#   destination  directory to populate, created if missing
#   cache        optional directory of verified downloads, reused before the
#                network and filled after it
#
# python3 (or python) is used to read the manifest. It is already a hard
# requirement of the web build, because emsdk is written in it.

set -eu

profile="${1:-desktop}"
destination="${2:-models}"
cache="${3:-}"
manifest="$(dirname "$0")/../assets/models.json"

if [ ! -f "$manifest" ]; then
    echo "Error: no manifest at $manifest" >&2
    exit 1
fi

python=""
for candidate in python3 python; do
    if command -v "$candidate" >/dev/null 2>&1 \
        && "$candidate" -c 'import sys; sys.exit(sys.version_info[0] != 3)' >/dev/null 2>&1; then
        python="$candidate"
        break
    fi
done
if [ -z "$python" ]; then
    echo "Error: python 3 is required to read $manifest" >&2
    exit 1
fi

mkdir -p "$destination"
if [ -n "$cache" ]; then
    mkdir -p "$cache"
fi

# One line per model: id, file, url, sha256. Tab separated so that a file name
# with a space in it would still parse, which no current entry has and no future
# one should.
entries="$("$python" - "$manifest" "$profile" <<'PY'
import json, sys

manifest, profile = sys.argv[1], sys.argv[2]
with open(manifest, encoding="utf-8") as handle:
    document = json.load(handle)

for model in document["models"]:
    if profile not in model["profiles"]:
        continue
    print("\t".join([model["id"], model["file"], model["url"], model["sha256"]]))
PY
)"

if [ -z "$entries" ]; then
    echo "Error: profile '$profile' selects no models" >&2
    exit 1
fi

checksum() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | cut -d' ' -f1
    else
        # macOS ships shasum rather than sha256sum.
        shasum -a 256 "$1" | cut -d' ' -f1
    fi
}

# A temporary file rather than a pipe into the loop: a `while` on the right of a
# pipeline runs in a subshell, so the failure counter it increments there would
# be discarded and this script would report success after a failed download.
list="$(mktemp)"
trap 'rm -f "$list"' EXIT INT TERM
# Python on Windows ends its lines with CRLF; the CR would stick to the sha256.
printf '%s\n' "$entries" | tr -d '\r' > "$list"

failures=0
while IFS="$(printf '\t')" read -r id file url expected; do
    target="$destination/$file"

    if [ -f "$target" ] && [ "$(checksum "$target")" = "$expected" ]; then
        echo "ok       $id  $file"
        continue
    fi

    if [ -n "$cache" ] && [ -f "$cache/$file" ] \
        && [ "$(checksum "$cache/$file")" = "$expected" ]; then
        cp -f "$cache/$file" "$target.part"
        mv -f "$target.part" "$target"
        echo "cached   $id  $file"
        continue
    fi

    echo "fetching $id  $file"
    # -4 for the same reason fetch-vcpkg-asset.sh has it: a host without IPv6
    # routing that resolves an AAAA-only mirror fails outright rather than
    # falling back.
    if ! curl -4 -L --fail --progress-bar --retry 3 --retry-connrefused \
              --connect-timeout 30 -o "$target.part" "$url"; then
        echo "Error: download failed for $id" >&2
        rm -f "$target.part"
        failures=$((failures + 1))
        continue
    fi

    actual="$(checksum "$target.part")"
    if [ "$actual" != "$expected" ]; then
        echo "Error: checksum mismatch for $id" >&2
        echo "  expected $expected" >&2
        echo "  actual   $actual" >&2
        rm -f "$target.part"
        failures=$((failures + 1))
        continue
    fi
    mv -f "$target.part" "$target"
    if [ -n "$cache" ] && [ "$cache/$file" != "$target" ]; then
        cp -f "$target" "$cache/$file.part" && mv -f "$cache/$file.part" "$cache/$file"
    fi
    echo "ok       $id  $file"
done < "$list"

if [ "$failures" -ne 0 ]; then
    echo "Error: $failures model(s) could not be provisioned" >&2
    exit 1
fi

echo "Models for profile '$profile' are in $destination"
