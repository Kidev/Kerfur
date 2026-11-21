#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Writes the CLIP text embeddings of assets/clip/vocabulary.json.

The image encoder runs on the accelerator; the text side runs here, once, in
full precision, with the open_clip weights the compiled encoder was made from.
Needs torch and open_clip_torch.

Usage: clip-labels.py [output]
"""

import base64
import hashlib
import json
import re
import struct
import sys
from pathlib import Path

import open_clip
import torch

ROOT = Path(__file__).resolve().parent.parent
VOCABULARY = ROOT / "assets" / "clip" / "vocabulary.json"
MANIFEST = ROOT / "assets" / "models.json"

# The compiled encoder and the weights it was compiled from.
ENCODER_FILE = "clip_vit_l_14_laion2B_image_encoder.hef"
ARCHITECTURE = "ViT-L-14"
PRETRAINED = "laion2b_s32b_b82k"
# CLIP's learned logit scale, rounded as every published zero-shot result uses it.
LOGIT_SCALE = 100.0


# How often an LVIS category occurs, most often first: when two categories
# mean the same thing, the label of the commoner one is kept.
FREQUENCY_ORDER = {"f": 0, "c": 1, "r": 2}


def encode(model, tokenizer, prompts):
    with torch.no_grad():
        rows = model.encode_text(tokenizer(prompts)).float()
    rows = rows / rows.norm(dim=-1, keepdim=True)
    mean = rows.mean(dim=0)
    return mean / mean.norm()


def pack(rows):
    """Half precision, little endian: the similarities they feed differ by
    less than the encoder's own quantisation."""
    flat = [value for row in rows for value in row.tolist()]
    return base64.b64encode(struct.pack("<%de" % len(flat), *flat)).decode("ascii")


def spoken(name):
    return name.replace("_", " ")


def label_of(name):
    """An LVIS name as a label: underscores spoken, the disambiguation dropped."""
    return spoken(re.sub(r"_\([^)]*\)$", "", name))


def with_article(noun):
    return ("an " if noun[0] in "aeiou" else "a ") + noun


def extra_classes(path, taken, exclude):
    """The classes of an extra file not already named by the set's own list
    and not ruled out by `exclude`, commonest first."""
    excluded_kinds = set(exclude.get("kindsOf", []))
    excluded_names = set(exclude.get("names", []))
    categories = [category for category in json.loads(path.read_text(encoding="utf-8"))["categories"]
                  if category["name"] not in excluded_names
                  and not excluded_kinds.intersection(category.get("kinds", []))]
    ordered = sorted(enumerate(categories),
                     key=lambda item: (FREQUENCY_ORDER.get(item[1]["frequency"], 3), item[0]))
    classes = []
    for _, category in ordered:
        names = category["synonyms"] or [category["name"]]
        label = label_of(category["name"])
        if label.lower() in taken or any(label_of(name).lower() in taken for name in names):
            continue
        classes.append((label, [with_article(spoken(name)) for name in names]))
    return classes


def vocabulary_digest(vocabulary_bytes, vocabulary):
    """The sha256 of the vocabulary followed by every extra class file it
    names, in the order its sets name them."""
    digest = hashlib.sha256(vocabulary_bytes)
    for name, entry in vocabulary.items():
        if not name.startswith("$") and "extraClasses" in entry:
            digest.update(VOCABULARY.with_name(entry["extraClasses"]).read_bytes())
    return digest.hexdigest()


def main():
    output = Path(sys.argv[1]) if len(sys.argv) > 1 else VOCABULARY.with_name(
        Path(ENCODER_FILE).stem + ".json")
    vocabulary_bytes = VOCABULARY.read_bytes()
    vocabulary = json.loads(vocabulary_bytes)
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    encoder = next(entry for entry in manifest["models"] if entry["file"] == ENCODER_FILE)

    model, _, _ = open_clip.create_model_and_transforms(ARCHITECTURE, pretrained=PRETRAINED)
    model.eval()
    tokenizer = open_clip.get_tokenizer(ARCHITECTURE)

    sets = {}
    for name, entry in vocabulary.items():
        if name.startswith("$"):
            continue
        classes = [(item["label"], [item["noun"]]) for item in entry["classes"]]
        if "extraClasses" in entry:
            taken = {label.lower() for label, _ in classes}
            classes += extra_classes(VOCABULARY.with_name(entry["extraClasses"]), taken,
                                     entry.get("exclude", {}))

        labels = []
        rows = []
        representatives = []
        threshold = entry.get("mergeAbove", 1.0)
        for label, nouns in classes:
            row = encode(model, tokenizer,
                         [template.format(noun) for noun in nouns for template in entry["templates"]])
            # The first label close enough stands for this one, so that
            # synonyms add their probability to one answer.
            match = next((index for index in representatives
                          if float(rows[index] @ row) >= threshold), None)
            if match is None:
                representatives.append(len(rows))
            else:
                label = labels[match]
            labels.append(label)
            rows.append(row)
        if entry["none"]:
            labels.append("")
            rows.append(encode(model, tokenizer, entry["none"]))
        sets[name] = {"labels": labels, "embeddings": pack(rows)}

    document = {
        "$comment": "Generated by cmake/clip-labels.py from vocabulary.json; do not edit.",
        "encoder": ENCODER_FILE,
        "encoderSha256": encoder["sha256"],
        "weights": "%s %s" % (ARCHITECTURE, PRETRAINED),
        "vocabularySha256": vocabulary_digest(vocabulary_bytes, vocabulary),
        "dimension": len(rows[0]),
        "dtype": "float16",
        "logitScale": LOGIT_SCALE,
        "sets": sets,
    }
    output.write_text(json.dumps(document, indent=1) + "\n", encoding="ascii")
    print("wrote", output)


if __name__ == "__main__":
    main()
