import hashlib
import importlib.util
import json
import tempfile
import unittest
import zipfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "verify_release_approval", ROOT / "scripts" / "verify_release_approval.py"
)
VERIFY_RELEASE_APPROVAL = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(VERIFY_RELEASE_APPROVAL)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


class ReleaseApprovalTests(unittest.TestCase):
    def make_release(self, root: Path) -> tuple[Path, Path]:
        version = "1.2.3"
        source_tree = "5" * 40
        installer = root / "installer"
        firmware_dir = installer / "firmware"
        firmware_dir.mkdir(parents=True)
        ota_name = f"transitink-zectrix-note4-ota-v{version}.bin"
        merged_name = f"transitink-zectrix-note4-v{version}.bin"
        bundle_name = f"transitink-zectrix-note4-v{version}.zip"
        ota = b"approved ota bytes"
        merged = b"approved merged bytes"
        (firmware_dir / ota_name).write_bytes(ota)
        (firmware_dir / merged_name).write_bytes(merged)
        metadata = {
            "version": version,
            "board": "zectrix_note4",
            "chip_family": "ESP32-S3",
            "firmware": merged_name,
            "bundle": bundle_name,
            "size": len(merged),
            "sha256": sha256(merged),
            "ota_firmware": ota_name,
            "ota_size": len(ota),
            "ota_sha256": sha256(ota),
        }
        ota_manifest = {
            "schema_version": 1,
            "product": "transitink-os",
            "board": "zectrix_note4",
            "version": version,
            "firmware": f"firmware/{ota_name}",
            "size": len(ota),
            "sha256": sha256(ota),
        }
        checksums = f"{sha256(merged)}  {merged_name}\n{sha256(ota)}  {ota_name}\n"
        metadata_bytes = (json.dumps(metadata, indent=2) + "\n").encode()
        manifest_bytes = (json.dumps(ota_manifest, indent=2) + "\n").encode()
        checksum_bytes = checksums.encode()
        (installer / "release-metadata.json").write_bytes(metadata_bytes)
        (installer / "ota-manifest.json").write_bytes(manifest_bytes)
        (installer / "SHA256SUMS.txt").write_bytes(checksum_bytes)
        bundle_path = installer / bundle_name
        with zipfile.ZipFile(bundle_path, "w") as bundle:
            bundle.writestr(ota_name, ota)
            bundle.writestr(merged_name, merged)
            bundle.writestr("release-metadata.json", metadata_bytes)
            bundle.writestr("ota-manifest.json", manifest_bytes)
            bundle.writestr("SHA256SUMS.txt", checksum_bytes)

        artifacts = {}
        for name, path in {
            "ota": firmware_dir / ota_name,
            "merged": firmware_dir / merged_name,
            "bundle": bundle_path,
            "ota_manifest": installer / "ota-manifest.json",
            "release_metadata": installer / "release-metadata.json",
            "checksums": installer / "SHA256SUMS.txt",
        }.items():
            artifacts[name] = {
                "path": path.relative_to(installer).as_posix(),
                "size": path.stat().st_size,
                "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
            }
        approval = {
            "schema_version": 1,
            "tag": f"v{version}",
            "version": version,
            "board": "zectrix_note4",
            "source_tree": source_tree,
            "artifacts": artifacts,
        }
        approval_path = root / "approval.json"
        approval_path.write_text(json.dumps(approval), encoding="utf-8")
        return installer, approval_path

    def test_accepts_exact_approved_release(self):
        with tempfile.TemporaryDirectory() as directory:
            installer, approval = self.make_release(Path(directory))
            result = VERIFY_RELEASE_APPROVAL.verify_release(
                approval, installer, "5" * 40
            )
            self.assertEqual("1.2.3", result["version"])
            self.assertEqual("zectrix_note4", result["board"])
            self.assertEqual(
                "transitink-zectrix-note4-ota-v1.2.3.bin",
                result["ota_filename"],
            )
            self.assertEqual(len(b"approved ota bytes"), result["ota_size"])

    def test_rejects_substituted_ota_bytes(self):
        with tempfile.TemporaryDirectory() as directory:
            installer, approval = self.make_release(Path(directory))
            ota = installer / "firmware" / "transitink-zectrix-note4-ota-v1.2.3.bin"
            ota.write_bytes(b"different workflow rebuild")
            with self.assertRaisesRegex(ValueError, "ota size differs from approval"):
                VERIFY_RELEASE_APPROVAL.verify_release(
                    approval, installer, "5" * 40
                )

    def test_rejects_wrong_source_tree(self):
        with tempfile.TemporaryDirectory() as directory:
            installer, approval = self.make_release(Path(directory))
            with self.assertRaisesRegex(ValueError, "source tree differs"):
                VERIFY_RELEASE_APPROVAL.verify_release(
                    approval, installer, "6" * 40
                )


if __name__ == "__main__":
    unittest.main()
