#!/bin/sh
# vcpkg asset download helper.
#
# Some upstream distfile hosts (kernel.org GeoDNS in particular) answer with a
# mirror that only publishes an AAAA record. On a machine without working IPv6
# connectivity the download then fails outright instead of falling back, which
# aborts the whole dependency install. Retry over IPv4 before giving up.
#
# Usage: fetch-vcpkg-asset.sh <url> <destination>
# Exiting non-zero lets vcpkg fall back to its own downloader.

set -eu

url="$1"
dst="$2"
tmp="${dst}.download"

mkdir -p "$(dirname "$dst")"
rm -f "$tmp"

for family in "" "-4"; do
    # shellcheck disable=SC2086 # $family is an intentional flag expansion
    if curl $family -L --fail --silent --show-error --retry 3 \
            --retry-connrefused --connect-timeout 30 -o "$tmp" "$url"; then
        mv -f "$tmp" "$dst"
        exit 0
    fi
    rm -f "$tmp"
done

exit 1
