#!/bin/sh
# Packs the web models into one data file for web/index.html.
#
# The page downloads the data file, keeps it in IndexedDB and, before the
# application starts, writes each model into the virtual file system under
# /models. The list printed into the list file gives each model's path and its
# byte range in the data file, as JSON.
#
# Usage: pack-web-models.sh <models> <data> <list>
#   models  directory filled by `make models-web`
#   data    data file to write; left absent when there is no model
#   list    JSON list to write, `[]` when there is no model

set -eu

if [ $# -ne 3 ]; then
    echo "usage: $0 <models> <data> <list>" >&2
    exit 2
fi

models="$1"
data="$2"
list="$3"

rm -f "$data" "$data.partial" "$list" "$list.partial"

if [ ! -d "$models" ]; then
    echo "No models in $models: the web build starts without perception" >&2
    printf '[]' > "$list"
    exit 0
fi

names="$(mktemp)"
trap 'rm -f "$names" "$data.partial" "$list.partial"' EXIT

# Byte order, so the same models always give the same data file and hash.
(cd "$models" && find . -type f ! -name '.*' -print) | sed 's#^\./##' | LC_ALL=C sort > "$names"

: > "$data.partial"
printf '[' > "$list.partial"
offset=0
separator=''
while IFS= read -r name; do
    # The list is pasted into the page as JavaScript.
    case "$name" in
        *[!A-Za-z0-9._/-]*)
            echo "Unsupported character in model file name: $name" >&2
            exit 1
            ;;
    esac
    size=$(wc -c < "$models/$name" | tr -d ' ')
    cat "$models/$name" >> "$data.partial"
    end=$((offset + size))
    printf '%s{"path":"/models/%s","start":%s,"end":%s}' "$separator" "$name" "$offset" "$end" \
        >> "$list.partial"
    separator=','
    offset=$end
done < "$names"
printf ']' >> "$list.partial"

if [ "$offset" -gt 0 ]; then
    mv "$data.partial" "$data"
else
    echo "No models in $models: the web build starts without perception" >&2
fi
mv "$list.partial" "$list"
echo "Packed $offset bytes of models into $data"
