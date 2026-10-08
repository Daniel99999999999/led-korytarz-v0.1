#!/usr/bin/env python3

"""Validate a tagged OTA build and prepare its public release assets."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
from pathlib import Path
from typing import Any, Dict

from ota_upload import EXPECTED_PROJECT, OtaUploadError, parse_app_descriptor


OTA_PROFILE = "esp32doit-devkit-v1-ota"
OTA_SLOT_BYTES = 0x1D0000
TAG_PATTERN = re.compile(r"v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\Z")
COMMIT_PATTERN = re.compile(r"[0-9a-f]{40}\Z")
VERSION_PATTERN = re.compile(r'^set\(PROJECT_VER "([^"]+)"\)\s*$', re.MULTILINE)


def validate_source_version(tag: str, cmake_path: Path) -> str:
    match = TAG_PATTERN.fullmatch(tag)
    if match is None:
        raise OtaUploadError("Tag wydania musi miec format vMAJOR.MINOR.PATCH")
    versions = VERSION_PATTERN.findall(cmake_path.read_text(encoding="utf-8"))
    if len(versions) != 1 or versions[0] != tag[1:]:
        raise OtaUploadError("Tag %s nie zgadza sie z PROJECT_VER w CMakeLists.txt" % tag)
    return versions[0]


def make_manifest(tag: str, image: bytes, commit: str) -> Dict[str, Any]:
    if COMMIT_PATTERN.fullmatch(commit) is None:
        raise OtaUploadError("Niepoprawny SHA commitu zrodel")
    if not image or image[0] != 0xE9 or len(image) > OTA_SLOT_BYTES:
        raise OtaUploadError("Obraz ESP32 jest pusty, niepoprawny lub za duzy dla slotu OTA")
    project, version = parse_app_descriptor(image)
    if version != tag[1:]:
        raise OtaUploadError("Wersja obrazu %s nie zgadza sie z tagiem %s" % (version, tag))
    return {
        "schema": 1,
        "project": project,
        "version": version,
        "tag": tag,
        "profile": OTA_PROFILE,
        "source_commit": commit,
        "filename": "firmware.bin",
        "size": len(image),
        "sha256": hashlib.sha256(image).hexdigest(),
    }


def write_release_assets(image_path: Path, output_dir: Path, manifest: Dict[str, Any]) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(image_path, output_dir / "firmware.bin")
    (output_dir / "release.json").write_text(
        json.dumps(manifest, indent=2, ensure_ascii=True) + "\n", encoding="utf-8"
    )
    (output_dir / "SHA256SUMS").write_text(
        "%s  firmware.bin\n" % manifest["sha256"], encoding="ascii"
    )
    (output_dir / "RELEASE_NOTES.md").write_text(
        "Firmware `%s` %s.\n\n"
        "Source commit: `%s`  \n"
        "Build profile: `%s`  \n"
        "SHA-256 (`firmware.bin`): `%s`\n"
        % (
            EXPECTED_PROJECT,
            manifest["version"],
            manifest["source_commit"],
            manifest["profile"],
            manifest["sha256"],
        ),
        encoding="utf-8",
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("tag", help="Source tag, e.g. v0.10.1")
    parser.add_argument("--image", type=Path, help="Built OTA firmware.bin")
    parser.add_argument("--commit", help="40-character source commit SHA")
    parser.add_argument("--output-dir", type=Path, help="Directory for release assets")
    args = parser.parse_args()

    root = Path(__file__).resolve().parent.parent
    version = validate_source_version(args.tag, root / "CMakeLists.txt")
    if args.image is None:
        if args.commit is not None or args.output_dir is not None:
            parser.error("--commit and --output-dir require --image")
        print("Release tag and PROJECT_VER: %s" % version)
        return 0
    if args.commit is None or args.output_dir is None:
        parser.error("--image requires --commit and --output-dir")
    image_path = args.image.resolve()
    image = image_path.read_bytes()
    manifest = make_manifest(args.tag, image, args.commit)
    write_release_assets(image_path, args.output_dir, manifest)
    print("Release image: %s (%u B, SHA-256 %s)" % (
        version, manifest["size"], manifest["sha256"]
    ))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OtaUploadError, OSError) as error:
        raise SystemExit("ERROR Release: %s" % error) from error
