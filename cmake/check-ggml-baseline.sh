#!/bin/bash
# SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Fails when a ggml archive holds an instruction outside the -march its vcpkg
# triplet gives ggml through GGML_CPU_ARM_ARCH.
#
# Usage: check-ggml-baseline.sh <triplet file> <archive>...

set -euo pipefail

if [ "$#" -lt 2 ]; then
    echo "usage: $0 <triplet file> <archive>..." >&2
    exit 2
fi
triplet="$1"
shift

arch=$(grep -oP 'GGML_CPU_ARM_ARCH=\K[^)\s]+' "${triplet}" || true)

# Past Armv8.2 with dotprod and fp16: SVE, SME, i8mm, bf16 and LRCPC2.
beyond_v82='\t(stlur|ldapur|smmla|ummla|usmmla|usdot|sudot|bfdot|bfmmla|bfmlal[bt]|bfcvtn?2?|smstart)\b|\b(z\d+\.[bhsdq]|p\d+/[zm])\b'
# Past Armv8.0: also LSE atomics, RDM, LRCPC, dotprod and fp16 arithmetic.
beyond_v80="${beyond_v82}"'|\t(casp?|ldadd|ldclr|ldeor|ldset|ld[su]max|ld[su]min|swp|stadd|stclr|steor|stset|st[su]max|st[su]min)[alhb]*\s|\tsqrdml[as]h\b|\tldapr|\t[su]dot\b|\tf(?!cvt)[a-z0-9]*\s+(v\d+\.[48]h|h\d+)\b'

case "${arch}" in
armv8-a) pattern="${beyond_v80}" ;;
armv8.2-a+dotprod+fp16) pattern="${beyond_v82}" ;;
*)
    echo "${triplet}: no instruction list for GGML_CPU_ARM_ARCH '${arch}'" >&2
    exit 2
    ;;
esac

for archive in "$@"; do
    if [ ! -f "${archive}" ]; then
        echo "No ggml archive at ${archive}" >&2
        exit 2
    fi
done

disassembly=$(mktemp)
trap 'rm -f "${disassembly}"' EXIT
objdump -d --no-show-raw-insn "$@" >"${disassembly}"

found=$(grep -P "${pattern}" "${disassembly}" || true)
if [ -n "${found}" ]; then
    echo "ggml holds instructions outside ${arch}:"
    head -n 20 <<<"${found}"
    exit 1
fi
echo "ggml stays within ${arch}"
