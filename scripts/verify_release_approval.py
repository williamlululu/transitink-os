#!/usr/bin/env python3
"""Fail closed unless a packaged release exactly matches its approval record."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
import zipfile
from pathlib import Path, PurePosixPath


SHA256_PATTERN = re.compile(r"^[0-9a-f]{64}$")
TREE_PATTERN = re.compile(r"^[0-9a-f]{40}$")
REQUIRED_ARTIFACTS = {
    "ota",
    "merged",
    "bundle",
    "ota_manifest",
    "release_metadata",
    "checksums",
}


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def read_json(path: Path) -> dict[str, object]:
    payload = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(payload, dict):
        raise ValueError(f"JSON root must be an object: {path}")
    return payload


def safe_path(root: Path, value: object) -> Path:
    if not isinstance(value, str):
        raise ValueError("approved artifact path must be a string")
    relative = PurePosixPath(value)
    if relative.is_absolute() or ".." in relative.parts or not relative.parts:
        raise ValueError(f"unsafe approved artifact path: {value}")
    return root.joinpath(*relative.parts)


def require_artifact(
    installer_dir: Path, name: str, approval: object
) -> tuple[Path, str]:
    if not isinstance(approval, dict):
        raise ValueError(f"approval entry {name} must be an object")
    if set(approval) != {"path", "size", "sha256"}:
        raise ValueError(f"approval entry {name} has unexpected fields")
    path = safe_path(installer_dir, approval["path"])
    if not path.is_file():
        raise FileNotFoundError(f"approved release artifact is missing: {path}")
    expected_size = approval["size"]
    expected_hash = approval["sha256"]
    if not isinstance(expected_size, int) or expected_size < 0:
        raise ValueError(f"approval entry {name} has an invalid size")
    if not isinstance(expected_hash, str) or SHA256_PATTERN.fullmatch(expected_hash) is None:
        raise ValueError(f"approval entry {name} has an invalid SHA-256")
    actual_size = path.stat().st_size
    actual_hash = sha256(path)
    if actual_size != expected_size:
        raise ValueError(
            f"{name} size differs from approval: expected {expected_size}, got {actual_size}"
        )
    if actual_hash != expected_hash:
        raise ValueError(
            f"{name} SHA-256 differs from approval: expected {expected_hash}, got {actual_hash}"
        )
    return path, actual_hash


def checksum_entries(text: str) -> dict[str, str]:
    entries: dict[str, str] = {}
    for line in text.splitlines():
        digest, separator, filename = line.partition("  ")
        if (
            separator != "  "
            or SHA256_PATTERN.fullmatch(digest) is None
            or not filename
            or filename in entries
        ):
            raise ValueError("SHA256SUMS contains an invalid entry")
        entries[filename] = digest
    return entries


def verify_bundle(
    bundle: Path,
    artifacts: dict[str, object],
    installer_dir: Path,
) -> None:
    ota_path = safe_path(installer_dir, artifacts["ota"]["path"])
    merged_path = safe_path(installer_dir, artifacts["merged"]["path"])
    expected_members = {
        ota_path.name: sha256(ota_path),
        merged_path.name: sha256(merged_path),
        "ota-manifest.json": sha256(installer_dir / "ota-manifest.json"),
        "release-metadata.json": sha256(installer_dir / "release-metadata.json"),
        "SHA256SUMS.txt": sha256(installer_dir / "SHA256SUMS.txt"),
    }
    with zipfile.ZipFile(bundle) as archive:
        names = set(archive.namelist())
        for name, expected_hash in expected_members.items():
            if name not in names:
                raise ValueError(f"release bundle is missing approved member: {name}")
            actual_hash = sha256_bytes(archive.read(name))
            if actual_hash != expected_hash:
                raise ValueError(f"release bundle member differs from approved file: {name}")


def verify_release(
    approval_path: Path,
    installer_dir: Path,
    source_tree: str | None = None,
) -> dict[str, object]:
    approval = read_json(approval_path)
    if approval.get("schema_version") != 1:
        raise ValueError("unsupported release approval schema")
    tag = approval.get("tag")
    version = approval.get("version")
    board = approval.get("board")
    approved_tree = approval.get("source_tree")
    if tag != f"v{version}" or not isinstance(version, str):
        raise ValueError("release approval tag/version mismatch")
    if board != "zectrix_note4":
        raise ValueError("release approval board mismatch")
    if not isinstance(approved_tree, str) or TREE_PATTERN.fullmatch(approved_tree) is None:
        raise ValueError("release approval source tree is invalid")
    if source_tree is not None and source_tree != approved_tree:
        raise ValueError(
            f"source tree differs from approval: expected {approved_tree}, got {source_tree}"
        )

    artifacts = approval.get("artifacts")
    if not isinstance(artifacts, dict) or set(artifacts) != REQUIRED_ARTIFACTS:
        raise ValueError("release approval artifact set is incomplete")
    verified: dict[str, tuple[Path, str]] = {}
    for name in sorted(REQUIRED_ARTIFACTS):
        verified[name] = require_artifact(installer_dir, name, artifacts[name])

    metadata = read_json(verified["release_metadata"][0])
    ota_manifest = read_json(verified["ota_manifest"][0])
    ota_path, ota_hash = verified["ota"]
    merged_path, merged_hash = verified["merged"]
    bundle_path, bundle_hash = verified["bundle"]
    expected_metadata = {
        "version": version,
        "board": board,
        "chip_family": "ESP32-S3",
        "firmware": merged_path.name,
        "bundle": bundle_path.name,
        "size": merged_path.stat().st_size,
        "sha256": merged_hash,
        "ota_firmware": ota_path.name,
        "ota_size": ota_path.stat().st_size,
        "ota_sha256": ota_hash,
    }
    if metadata != expected_metadata:
        raise ValueError("release metadata differs from approved artifacts")
    expected_ota_manifest = {
        "schema_version": 1,
        "product": "transitink-os",
        "board": board,
        "version": version,
        "firmware": f"firmware/{ota_path.name}",
        "size": ota_path.stat().st_size,
        "sha256": ota_hash,
    }
    if ota_manifest != expected_ota_manifest:
        raise ValueError("OTA manifest differs from approved artifact")
    checksums = checksum_entries(verified["checksums"][0].read_text(encoding="utf-8"))
    if checksums != {merged_path.name: merged_hash, ota_path.name: ota_hash}:
        raise ValueError("SHA256SUMS differs from approved artifacts")
    verify_bundle(bundle_path, artifacts, installer_dir)
    return {
        "tag": tag,
        "version": version,
        "board": board,
        "source_tree": approved_tree,
        "ota_filename": ota_path.name,
        "ota_size": ota_path.stat().st_size,
        "ota_sha256": ota_hash,
        "merged_filename": merged_path.name,
        "merged_size": merged_path.stat().st_size,
        "merged_sha256": merged_hash,
        "bundle_filename": bundle_path.name,
        "bundle_size": bundle_path.stat().st_size,
        "bundle_sha256": bundle_hash,
    }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--approval", type=Path, required=True)
    parser.add_argument("--installer-dir", type=Path, required=True)
    parser.add_argument("--source-tree")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        result = verify_release(
            args.approval.resolve(),
            args.installer_dir.resolve(),
            args.source_tree,
        )
    except (FileNotFoundError, ValueError, json.JSONDecodeError, zipfile.BadZipFile) as error:
        print(f"release approval verification failed: {error}", file=sys.stderr)
        return 1
    print(json.dumps(result, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
