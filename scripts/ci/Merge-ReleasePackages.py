"""Verify both compiler builds before publishing one release."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil


def digest(path):
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def assemble(source, destination, version, commit, qt_version="6.12.0"):
    source, destination = Path(source), Path(destination)
    if not re.fullmatch(r"(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)", version):
        raise ValueError("Invalid release version")
    if not re.fullmatch(r"[a-f0-9]{40}", commit):
        raise ValueError("Invalid source commit")
    if not re.fullmatch(r"6\.12\.\d+", qt_version):
        raise ValueError("Expected a Qt 6.12 version")
    if destination.exists() and any(destination.iterdir()):
        raise ValueError("Release output must be empty")
    prefix = f"7ZipContext-{version}-windows-x64"
    variants, verified = {}, []
    expected_files = set()
    for toolchain in ("msvc", "ucrt"):
        metadata_name = f"build-info-{toolchain}.json"
        names = {f"{prefix}-{toolchain}.msi", f"{prefix}-{toolchain}-portable.zip", metadata_name}
        checksum_name = f"SHA256SUMS-{toolchain}.txt"
        expected_files.update(names | {checksum_name})
        lines = (source / checksum_name).read_text(encoding="utf-8-sig").splitlines()
        hashes = {}
        for line in lines:
            match = re.fullmatch(r"([a-fA-F0-9]{64})  (.+)", line)
            if not match or match[2] not in names or match[2] in hashes:
                raise ValueError(f"Invalid or duplicate checksum entry: {toolchain}")
            hashes[match[2]] = match[1].lower()
        if hashes.keys() != names:
            raise ValueError(f"Incomplete checksums: {toolchain}")
        for name, checksum in hashes.items():
            path = source / name
            if path.is_symlink() or not path.is_file() or digest(path) != checksum:
                raise ValueError(f"Checksum mismatch: {name}")
        metadata = json.loads((source / metadata_name).read_text(encoding="utf-8-sig"))
        if any((metadata.get("version") != version, metadata.get("commit") != commit,
                metadata.get("toolchain") != toolchain, metadata.get("architecture") != "x64",
                metadata.get("dirtyWorkingTree") is not False,
                metadata.get("qtVersion") != qt_version,
                metadata.get("compilerId") != ("MSVC" if toolchain == "msvc" else "GNU"),
                not metadata.get("compilerVersion"), metadata.get("portableSmokeTest") != "passed")):
            raise ValueError(f"Build identity or verification mismatch: {toolchain}")
        variants[toolchain] = metadata
        verified.extend(names)
    if {path.name for path in source.iterdir()} != expected_files:
        raise ValueError("Unexpected release artifact files")
    destination.mkdir(parents=True, exist_ok=True)
    for name in sorted(verified):
        shutil.copyfile(source / name, destination / name)
    # Older installed versions request the original filenames. Keep them on MSVC.
    for suffix in (".msi", "-portable.zip"):
        shutil.copyfile(destination / f"{prefix}-msvc{suffix}", destination / f"{prefix}{suffix}")
    (destination / "build-info.json").write_text(json.dumps({
        "version": version, "commit": commit, "qtVersion": qt_version, "variants": variants,
    }, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    checksums = [f"{digest(path)}  {path.name}" for path in sorted(destination.iterdir())]
    (destination / "SHA256SUMS.txt").write_text("\n".join(checksums) + "\n", encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--version", required=True)
    parser.add_argument("--commit", required=True)
    parser.add_argument("--qt-version", default="6.12.0")
    args = parser.parse_args()
    assemble(args.source, args.destination, args.version, args.commit, args.qt_version)
