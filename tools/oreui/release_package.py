"""Build and verify a complete, player-installable Windows release.

The six validated native LHolo files are copied from bin/LHolo, and the
installer, bilingual guide, credits, and optional Realm Companion come from
version-controlled sources. Never ship user config, logs, or debug symbols.
GitHub Actions zips the curated dist/ directory without repacking on publish.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import zipfile

PROJECT = Path(__file__).resolve().parents[2]
FILES = (
    "LHolo/LHolo.dll", "LHolo/manifest.json", "LHolo/LICENSE",
    "LHolo/resource_packs/LHoloUI/manifest.json",
    "LHolo/resource_packs/LHoloUI/hbui/lholo-menu.html",
    "LHolo/resource_packs/LHoloUI/hbui/lholo-menu.js",
)
MARKER = b"lholo-menu-v7-settings"

# Relative ZIP path -> tracked source-file path.
RELEASE_SOURCES = {
    "Instalar.ps1": "tools/install/Instalar.ps1",
    "INSTALL.md": "docs/INSTALL_WINDOWS.md",
    "ATTRIBUTION.md": "ATTRIBUTION.md",
    "THIRD_PARTY_NOTICES.md": "THIRD_PARTY_NOTICES.md",
    "Litematica_Realm_Companion_BP/LICENSE": "LICENSE",
    "Litematica_Realm_Companion_BP/README.md": "realm_companion/README.md",
    "Litematica_Realm_Companion_BP/manifest.json": "realm_companion/manifest.json",
    "Litematica_Realm_Companion_BP/scripts/main.js": "realm_companion/scripts/main.js",
    "Litematica_Realm_Companion_BP/scripts/job.js": "realm_companion/scripts/job.js",
    "Litematica_Realm_Companion_BP/scripts/librarian.js": "realm_companion/scripts/librarian.js",
    "Litematica_Realm_Companion_BP/scripts/protocol.js": "realm_companion/scripts/protocol.js",
}
RELEASE_FILES = FILES + tuple(RELEASE_SOURCES)


def validate_files(files, version):
    """Validate only the original six native files assembled by xmake."""
    if set(files) != set(FILES):
        raise ValueError("Native installation must contain exactly the six LHolo files")
    if any(not content for content in files.values()):
        raise ValueError("Empty installation file")
    manifest = json.loads(files["LHolo/manifest.json"].decode("utf-8-sig"))
    expected = {"name": "LHolo", "entry": "LHolo.dll", "version": version,
                "type": "native", "platform": "client"}
    if any(manifest.get(key) != value for key, value in expected.items()):
        raise ValueError("Release native manifest mismatch")
    pack = json.loads(files["LHolo/resource_packs/LHoloUI/manifest.json"])
    template = json.loads((PROJECT / "tools/oreui/resource_pack.json").read_text(encoding="utf-8"))
    if (pack["format_version"] != template["format_version"]
            or pack["header"]["min_engine_version"] != template["header"]["min_engine_version"]):
        raise ValueError("Release resource pack format/engine mismatch")
    if len(pack["modules"]) != 1:
        raise ValueError("Unexpected resource pack modules")
    for section, original in ((pack["header"], template["header"]),
                              (pack["modules"][0], template["modules"][0])):
        if (section["uuid"] != original["uuid"]
                or section["version"] != list(map(int, version.split(".")))):
            raise ValueError("Release resource pack identity/version mismatch")
    if pack["modules"][0]["type"] != "resources":
        raise ValueError("Expected a resource pack module")
    for name in FILES[-2:]:
        if MARKER not in files[name]:
            raise ValueError("Release UI protocol mismatch")


def validate_release_files(files, version):
    if set(files) != set(RELEASE_FILES) or len(files) != len(RELEASE_FILES):
        missing = sorted(set(RELEASE_FILES) - set(files))
        extra = sorted(set(files) - set(RELEASE_FILES))
        raise ValueError(f"Release contains missing or extra files: missing={missing}; extra={extra}")
    if any(not content for content in files.values()):
        raise ValueError("Empty file in release package")
    validate_files({name: files[name] for name in FILES}, version)

    installer = files["Instalar.ps1"].decode("utf-8-sig")
    if not all(token in installer for token in (
            "LHolo.dll", "Litematica_Realm_Companion_BP", "SinCompanion", "Instancia")):
        raise ValueError("Player installer does not match the release directory layout")
    guide = files["INSTALL.md"].decode("utf-8-sig")
    if "Instalar.ps1" not in guide or "LeviLamina" not in guide:
        raise ValueError("Missing usable Windows installation guide")
    attribution = files["ATTRIBUTION.md"].decode("utf-8-sig")
    if "LHolo" not in attribution or "MarmieQi" not in attribution:
        raise ValueError("Required upstream attribution missing")

    companion = json.loads(files["Litematica_Realm_Companion_BP/manifest.json"].decode("utf-8-sig"))
    modules = companion.get("modules", [])
    if not any(m.get("type") == "script" and m.get("entry") == "scripts/main.js"
               for m in modules):
        raise ValueError("Realm Companion script entry missing")
    if len(companion.get("header", {}).get("version", [])) != 3:
        raise ValueError("Realm Companion version missing")


def checksum_path(archive):
    return archive.with_name(archive.name + ".sha256")


def source_inventory(version):
    """Read exactly the curated release files, checking native build digests."""
    report = json.loads((PROJECT / "build/oreui/package-validation.json").read_text(encoding="utf-8"))
    native = {name: (PROJECT / "bin" / name).read_bytes() for name in FILES}
    validate_files(native, version)
    if report["version"] != version or set(report["files"]) != set(FILES):
        raise ValueError("Run xmake for the release version first")
    for name, content in native.items():
        if hashlib.sha256(content).hexdigest() != report["files"][name]:
            raise ValueError(f"Outdated installation file: {name}; run xmake first")
    files = dict(native)
    files.update({name: (PROJECT / source).read_bytes()
                  for name, source in RELEASE_SOURCES.items()})
    validate_release_files(files, version)
    return files


def create_directory(directory, version):
    """Populate a clean directory for actions/upload-artifact to archive."""
    files = source_inventory(version)
    if directory.exists():
        raise ValueError(f"Output directory already exists: {directory}")
    directory.mkdir(parents=True)
    for name, content in files.items():
        target = directory / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(content)
    verify_directory(directory, version)


def verify_directory(directory, version):
    if not directory.is_dir():
        raise ValueError(f"Release directory missing: {directory}")
    actual = {path.relative_to(directory).as_posix(): path.read_bytes()
              for path in directory.rglob("*") if path.is_file()}
    validate_release_files(actual, version)


def verify_archive(archive, version):
    expected = checksum_path(archive).read_text(encoding="ascii").strip()
    actual = hashlib.sha256(archive.read_bytes()).hexdigest()
    if expected != f"{actual}  {archive.name}":
        raise ValueError("Release archive SHA256 mismatch")
    with zipfile.ZipFile(archive) as package:
        names = package.namelist()
        if len(names) != len(RELEASE_FILES) or set(names) != set(RELEASE_FILES):
            raise ValueError("Release archive contains missing, extra or duplicate files")
        validate_release_files({name: package.read(name) for name in names}, version)


def create_archive(archive, version):
    files = source_inventory(version)
    archive.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED) as package:
        for name, content in files.items():
            package.writestr(name, content)
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    checksum_path(archive).write_text(f"{digest}  {archive.name}\n", encoding="ascii")
    verify_archive(archive, version)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--output", type=Path, help="Create a standalone ZIP and checksum")
    mode.add_argument("--verify", type=Path, help="Verify standalone ZIP and checksum")
    mode.add_argument("--directory", type=Path, help="Create exact player distribution folder")
    mode.add_argument("--check-directory", type=Path, help="Validate a player distribution folder")
    parser.add_argument("--tag", help="Expected release tag, for example v26.60.1")
    args = parser.parse_args()
    version = json.loads((PROJECT / "tooth.json").read_text(encoding="utf-8"))["version"]
    if args.tag and args.tag != "v" + version:
        raise ValueError("Release tag and tooth version differ")
    if args.output:
        create_archive(args.output, version)
        print(f"Created and verified player release ZIP: {args.output}")
    elif args.verify:
        verify_archive(args.verify, version)
        print(f"Verified player release ZIP: {args.verify}")
    elif args.directory:
        create_directory(args.directory, version)
        print(f"Created and verified player distribution: {args.directory} ({len(RELEASE_FILES)} files)")
    else:
        verify_directory(args.check_directory, version)
        print(f"Verified player distribution: {args.check_directory}")


if __name__ == "__main__":
    main()
