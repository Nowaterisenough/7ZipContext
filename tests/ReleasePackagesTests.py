import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("release_packages", Path(__file__).resolve().parents[1] / "scripts/ci/Merge-ReleasePackages.py")
packages = importlib.util.module_from_spec(spec)
spec.loader.exec_module(packages)


class ReleasePackagesTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.source = Path(self.temporary.name) / "source"
        self.output = Path(self.temporary.name) / "release"
        self.source.mkdir()
        self.version = "1.2.3"
        self.commit = "a" * 40
        self.prefix = f"7ZipContext-{self.version}-windows-x64"
        for toolchain in ("msvc", "ucrt"):
            for suffix in (".msi", "-portable.zip"):
                (self.source / f"{self.prefix}-{toolchain}{suffix}").write_bytes(toolchain.encode() + suffix.encode())
            (self.source / f"build-info-{toolchain}.json").write_text(json.dumps({
                "version": self.version, "commit": self.commit, "toolchain": toolchain,
                "architecture": "x64", "qtVersion": "6.12.0", "dirtyWorkingTree": False,
                "compilerId": "MSVC" if toolchain == "msvc" else "GNU", "compilerVersion": "1.2.3",
                "portableSmokeTest": "passed",
            }))
            self.checksums(toolchain)

    def checksums(self, toolchain):
        names = [f"{self.prefix}-{toolchain}.msi", f"{self.prefix}-{toolchain}-portable.zip", f"build-info-{toolchain}.json"]
        (self.source / f"SHA256SUMS-{toolchain}.txt").write_text("\n".join(
            f"{packages.digest(self.source / name)}  {name}" for name in names) + "\n")

    def assemble(self):
        packages.assemble(self.source, self.output, self.version, self.commit)

    def test_both_variants_and_legacy_aliases_are_verified(self):
        self.assemble()
        for suffix in (".msi", "-portable.zip"):
            self.assertEqual((self.output / f"{self.prefix}{suffix}").read_bytes(),
                             (self.output / f"{self.prefix}-msvc{suffix}").read_bytes())
            self.assertNotEqual((self.output / f"{self.prefix}-ucrt{suffix}").read_bytes(),
                                (self.output / f"{self.prefix}{suffix}").read_bytes())
        checksums = (self.output / "SHA256SUMS.txt").read_text().splitlines()
        self.assertEqual(len(checksums), 9)
        for line in checksums:
            checksum, name = line.split("  ", 1)
            self.assertEqual(checksum, packages.digest(self.output / name))

    def test_tampered_package_is_rejected_before_output(self):
        (self.source / f"{self.prefix}-ucrt.msi").write_bytes(b"damaged")
        with self.assertRaisesRegex(ValueError, "Checksum mismatch"):
            self.assemble()
        self.assertFalse(self.output.exists())

    def test_mismatched_identity_is_rejected_even_with_valid_hash(self):
        path = self.source / "build-info-ucrt.json"
        original = json.loads(path.read_text())
        for key, value in {"version": "1.2.4", "commit": "b" * 40, "toolchain": "msvc", "qtVersion": "6.13.0",
                           "compilerId": "MSVC", "dirtyWorkingTree": True, "portableSmokeTest": "failed"}.items():
            with self.subTest(key=key):
                path.write_text(json.dumps(original | {key: value}))
                self.checksums("ucrt")
                with self.assertRaisesRegex(ValueError, "identity"):
                    self.assemble()
                self.assertFalse(self.output.exists())

    def test_missing_variant_is_rejected(self):
        (self.source / "SHA256SUMS-ucrt.txt").unlink()
        with self.assertRaises(FileNotFoundError):
            self.assemble()
        self.assertFalse(self.output.exists())

    def test_duplicate_or_traversal_checksum_is_rejected(self):
        path = self.source / "SHA256SUMS-ucrt.txt"
        original = path.read_text()
        for extra in (original.splitlines()[0], "a" * 64 + "  ../other.msi"):
            path.write_text(original + extra + "\n")
            with self.assertRaisesRegex(ValueError, "checksum entry"):
                self.assemble()
            self.assertFalse(self.output.exists())


if __name__ == "__main__":
    unittest.main()
