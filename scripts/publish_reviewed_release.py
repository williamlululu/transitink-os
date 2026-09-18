#!/usr/bin/env python3
"""Publish only reviewed v1.3.1 bytes; never build firmware or attest a new build."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import zipfile

from assemble_installer_pages import assemble_pages
from verify_release_approval import verify_release

ROOT = Path(__file__).resolve().parents[1]
REPOSITORY = "williamlululu/transitink-os"
TAG = "v1.3.1"
STAGING_TAG = "staging/v1.3.1-reviewed"
CHECKPOINT = "87ef2e7d3d65fac594de5e8ada9e725865cd8e29"
TREE = "7163e26123086af8e656a501f7f1970a647c73d7"
SOURCE_NAME = "transitink-os-1.3.1-source.zip"
SOURCE_SHA256 = "cc22c78fd79609538426ef6689093dac391902288d4e1dc91ed00e5ca336fc64"
LEGAL = ["LICENSE.txt", "THIRD_PARTY_NOTICES.md", "THIRD_PARTY_DATA.md", "CORRESPONDING_SOURCE.md"]


def run(*args):
    return subprocess.check_output(args, cwd=ROOT, text=True).strip()


def api(path):
    return json.loads(run("gh", "api", f"repos/{REPOSITORY}/{path}"))


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def require(condition, message):
    if not condition:
        raise ValueError(message)


def main():
    require(os.environ.get("GITHUB_ACTIONS") == "true", "Only run via the explicit publication workflow")
    require(os.environ.get("GH_REPO") == REPOSITORY, "Wrong publication repository")
    staging_id = os.environ.get("STAGING_RELEASE_ID", "")
    require(staging_id.isdigit(), "Staging release ID must be numeric")
    approval_path = ROOT / "release/approved/v1.3.1.json"
    approval = json.loads(approval_path.read_text(encoding="utf-8"))
    require(approval["source_checkpoint"] == CHECKPOINT and approval["source_tree"] == TREE, "Wrong reviewed source identity")
    require(run("git", "rev-parse", f"{CHECKPOINT}^{{tree}}") == TREE, "Frozen source tree unavailable/mismatched")
    require(run("git", "merge-base", "--is-ancestor", CHECKPOINT, "HEAD") == "", "Publication metadata must descend from frozen source")
    staging = api(f"releases/{staging_id}")
    require(staging["draft"] and staging["tag_name"] == STAGING_TAG, "Not the approved staging draft")
    require(staging["target_commitish"] == CHECKPOINT, "Staging release source mismatch")
    expected = {Path(entry["path"]).name for entry in approval["artifacts"].values()} | {SOURCE_NAME} | set(LEGAL)
    names = [asset["name"] for asset in staging["assets"]]
    require(len(names) == len(expected) and set(names) == expected, "Unexpected/missing/duplicate staged assets")
    require(not api(f"git/matching-refs/tags/{TAG}"), "Existing release tag must not be overwritten")
    require(not any(item["tag_name"] == TAG for item in api("releases?per_page=100")), "Existing release must not be overwritten")
    download = ROOT / "dist/reviewed-download"
    require(not download.exists(), "Fresh download directory required")
    download.mkdir(parents=True)
    subprocess.run(["gh", "release", "download", STAGING_TAG, "--repo", REPOSITORY,
                    "--dir", str(download)], cwd=ROOT, check=True)
    require({p.name for p in download.iterdir()} == expected, "Downloaded asset set mismatch")
    for name, entry in approval["artifacts"].items():
        path = download / Path(entry["path"]).name
        require(path.stat().st_size == entry["size"] and digest(path) == entry["sha256"], "Reviewed artifact mismatch: " + name)
    require(digest(download / SOURCE_NAME) == SOURCE_SHA256, "Reviewed source archive mismatch")
    # The archive must describe the exact frozen Git tree, including executable modes.
    expected_tree = {}
    for row in subprocess.check_output(["git", "ls-tree", "-r", "-z", CHECKPOINT], cwd=ROOT).split(b"\0"):
        if row:
            metadata, name = row.split(b"\t")
            mode, kind, blob = metadata.decode().split()
            require(kind == "blob", "Unexpected source tree entry")
            expected_tree[name.decode()] = (mode, blob)
    actual = {}
    with zipfile.ZipFile(download / SOURCE_NAME) as archive:
        require(archive.testzip() is None, "Source ZIP CRC failed")
        for info in archive.infolist():
            if info.is_dir():
                continue
            require(info.filename.startswith("transitink-os-1.3.1/") and ".." not in info.filename.split("/"), "Unsafe source entry")
            name = info.filename.split("/", 1)[1]
            require(name not in actual, "Duplicate source entry")
            raw = archive.read(info)
            blob = hashlib.sha1(b"blob " + str(len(raw)).encode() + b"\0" + raw).hexdigest()
            mode = "100755" if ((info.external_attr >> 16) & 0o111) else "100644"
            actual[name] = (mode, blob)
    require(actual == expected_tree, "Source archive tree mismatch")
    bundle = download / Path(approval["artifacts"]["bundle"]["path"]).name
    site = ROOT / "dist/reviewed-installer"
    require(not site.exists(), "Fresh installer output required")
    assemble_pages(bundle, site)
    verified = verify_release(approval_path, site, TREE)
    for name in LEGAL:
        require((download / name).read_bytes() == (site / "legal" / name).read_bytes(), "Legal asset mismatch")
    # No tag/release mutation occurs until every source, bundle and payload gate passed.
    # The workflow's standard GITHUB_TOKEN prevents recursive tag-triggered rebuilding.
    subprocess.run(["git", "tag", TAG, CHECKPOINT], cwd=ROOT, check=True)
    subprocess.run(["git", "push", "origin", f"refs/tags/{TAG}"], cwd=ROOT, check=True)
    subprocess.run(["gh", "release", "create", TAG, "--repo", REPOSITORY, "--verify-tag",
                    "--title", "TransitInk OS v1.3.1", "--notes-file", str(ROOT / "release/notes/v1.3.1.md"),
                    "--latest"] + [str(download / name) for name in sorted(expected)], cwd=ROOT, check=True)
    print(json.dumps({"published": TAG, "firmware_source_checkpoint": CHECKPOINT,
        "firmware_source_tree": TREE, "publication_commit": run("git", "rev-parse", "HEAD"),
        "verified": verified, "firmware_rebuilt": False, "build_provenance_attestation_created": False}))


if __name__ == "__main__":
    main()
