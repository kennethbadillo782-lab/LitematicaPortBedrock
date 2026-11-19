import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import zipfile

SCRIPT = Path(__file__).resolve().parents[1] / "release_package.py"
spec = importlib.util.spec_from_file_location("lholo_release", SCRIPT)
release = importlib.util.module_from_spec(spec)
spec.loader.exec_module(release)


class ReleasePackageTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.archive = self.root / "release/LHolo-client-windows-x64.zip"
        self.version = "26.51.6"
        pack_source = SCRIPT.with_name("resource_pack.json").read_text(encoding="utf-8")
        pack = json.loads(pack_source)
        for section in (pack["header"], pack["modules"][0]):
            section["version"] = [26, 51, 6]
        self.files = dict(zip(release.FILES, (
            b"native fixture", json.dumps({"name": "LHolo", "entry": "LHolo.dll",
                "version": self.version, "type": "native", "platform": "client"}).encode(),
            b"license", json.dumps(pack).encode(), release.MARKER, release.MARKER)))
        for name, content in self.files.items():
            target = self.root / "bin" / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(content)
        # Fixtures for the installer, guide, attribution, and optional Companion.
        # Copy tracked source bytes so the release tests exercise the real files.
        original_project = SCRIPT.parents[2]
        for source in sorted(set(release.RELEASE_SOURCES.values())):
            destination = self.root / source
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes((original_project / source).read_bytes())
        template = self.root / "tools/oreui/resource_pack.json"
        template.parent.mkdir(parents=True)
        template.write_text(pack_source, encoding="utf-8")
        report = self.root / "build/oreui/package-validation.json"
        report.parent.mkdir(parents=True)
        report.write_text(json.dumps({"version": self.version,
            "files": {name: hashlib.sha256(data).hexdigest() for name, data in self.files.items()}}))
        self.patch = patch.object(release, "PROJECT", self.root)
        self.patch.start()
        self.addCleanup(self.patch.stop)

    def create(self):
        release.create_archive(self.archive, self.version)

    def test_complete_archive_excludes_user_and_debug_files(self):
        (self.root / "bin/LHolo/LHolo.pdb").write_bytes(b"debug")
        config = self.root / "bin/LHolo/config/config.json"
        config.parent.mkdir()
        config.write_text('{"user":"keep private"}')
        self.create()
        release.verify_archive(self.archive, self.version)
        with zipfile.ZipFile(self.archive) as package:
            self.assertEqual(set(package.namelist()), set(release.RELEASE_FILES))
            self.assertIn("Instalar.ps1", package.namelist())
            self.assertIn("INSTALL.md", package.namelist())
            self.assertIn("Litematica_Realm_Companion_BP/scripts/main.js", package.namelist())

    def test_changed_dll_rejected_before_archive_is_written(self):
        (self.root / "bin/LHolo/LHolo.dll").write_bytes(b"changed")
        with self.assertRaisesRegex(ValueError, "Outdated installation file"):
            self.create()
        self.assertFalse(self.archive.exists())

    def test_missing_ui_rejected(self):
        (self.root / "bin" / release.FILES[-1]).unlink()
        with self.assertRaises(FileNotFoundError):
            self.create()
        self.assertFalse(self.archive.exists())

    def test_wrong_release_version_rejected(self):
        self.create()
        with self.assertRaisesRegex(ValueError, "manifest mismatch"):
            release.verify_archive(self.archive, "26.51.5")

    def test_ui_marker_mismatch_rejected(self):
        self.files[release.FILES[-1]] = b"incompatible script"
        with self.assertRaisesRegex(ValueError, "UI protocol mismatch"):
            release.validate_files(self.files, self.version)

    def test_resource_pack_version_mismatch_rejected(self):
        pack = json.loads(self.files[release.FILES[3]])
        pack["header"]["version"] = [26, 51, 5]
        self.files[release.FILES[3]] = json.dumps(pack).encode()
        with self.assertRaisesRegex(ValueError, "identity/version mismatch"):
            release.validate_files(self.files, self.version)

    def test_corrupt_archive_rejected(self):
        self.create()
        self.archive.write_bytes(self.archive.read_bytes() + b"changed")
        with self.assertRaisesRegex(ValueError, "SHA256 mismatch"):
            release.verify_archive(self.archive, self.version)

    def test_unexpected_archive_member_rejected(self):
        self.create()
        with zipfile.ZipFile(self.archive, "a") as package:
            package.writestr("LHolo/config/config.json", b"user data")
        digest = hashlib.sha256(self.archive.read_bytes()).hexdigest()
        release.checksum_path(self.archive).write_text(f"{digest}  {self.archive.name}\n", encoding="ascii")
        with self.assertRaisesRegex(ValueError, "extra or duplicate"):
            release.verify_archive(self.archive, self.version)


    def test_curated_directory_has_installer_companion_and_no_private_data(self):
        (self.root / "bin/LHolo/LHolo.pdb").write_bytes(b"debug")
        config = self.root / "bin/LHolo/config/config.json"
        config.parent.mkdir()
        config.write_text('{"secret":"user setting"}')
        output = self.root / "dist"
        release.create_directory(output, self.version)
        release.verify_directory(output, self.version)
        actual = {path.relative_to(output).as_posix() for path in output.rglob("*") if path.is_file()}
        self.assertEqual(actual, set(release.RELEASE_FILES))
        self.assertNotIn("LHolo/config/config.json", actual)
        self.assertFalse(any(path.endswith(".pdb") for path in actual))
        with self.assertRaisesRegex(ValueError, "already exists"):
            release.create_directory(output, self.version)
        leaked = output / "LHolo/config/config.json"
        leaked.parent.mkdir()
        leaked.write_bytes(b"private")
        with self.assertRaisesRegex(ValueError, "missing or extra"):
            release.verify_directory(output, self.version)

    def test_missing_installer_rejected(self):
        (self.root / "tools/install/Instalar.ps1").unlink()
        with self.assertRaises(FileNotFoundError):
            self.create()
        self.assertFalse(self.archive.exists())

    def test_invalid_companion_rejected(self):
        manifest = self.root / "realm_companion/manifest.json"
        value = json.loads(manifest.read_text(encoding="utf-8"))
        for module in value["modules"]:
            if module.get("type") == "script":
                module["entry"] = "scripts/not-shipped.js"
        manifest.write_text(json.dumps(value), encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "script entry missing"):
            self.create()
        self.assertFalse(self.archive.exists())

    def test_missing_companion_and_installer_from_archive_rejected(self):
        self.create()
        with zipfile.ZipFile(self.archive) as archive:
            files = {name: archive.read(name) for name in archive.namelist()}
        for filename in ("Instalar.ps1", "Litematica_Realm_Companion_BP/scripts/main.js"):
            with self.subTest(filename=filename):
                damaged = dict(files)
                damaged.pop(filename)
                with self.assertRaisesRegex(ValueError, "missing or extra"):
                    release.validate_release_files(damaged, self.version)


if __name__ == "__main__":
    unittest.main()
