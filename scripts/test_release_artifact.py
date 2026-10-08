#!/usr/bin/env python3

"""Host tests for tagged release packaging and release-based OTA validation."""

import io
import json
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import ota_upload
import release_artifact


def sample_image(version: str = "0.10.1") -> bytes:
    image = bytearray(512)
    image[0] = 0xE9
    image[32:36] = ota_upload.APP_DESCRIPTOR_MAGIC
    image[48 : 48 + len(version)] = version.encode("ascii")
    project = ota_upload.EXPECTED_PROJECT.encode("ascii")
    image[80 : 80 + len(project)] = project
    return bytes(image)


class ReleaseArtifactTests(unittest.TestCase):
    def test_tag_must_match_source_version(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            cmake = Path(directory) / "CMakeLists.txt"
            cmake.write_text('set(PROJECT_VER "0.10.1")\n', encoding="utf-8")
            self.assertEqual(
                release_artifact.validate_source_version("v0.10.1", cmake), "0.10.1"
            )
            with self.assertRaises(ota_upload.OtaUploadError):
                release_artifact.validate_source_version("v0.10.2", cmake)
            with self.assertRaises(ota_upload.OtaUploadError):
                release_artifact.validate_source_version("v0.10.1-test", cmake)

    def test_manifest_and_assets_match_binary(self) -> None:
        image = sample_image()
        commit = "a" * 40
        manifest = release_artifact.make_manifest("v0.10.1", image, commit)
        self.assertEqual(manifest["version"], "0.10.1")
        self.assertEqual(manifest["size"], len(image))
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "source.bin"
            source.write_bytes(image)
            output = Path(directory) / "out"
            release_artifact.write_release_assets(source, output, manifest)
            self.assertEqual((output / "firmware.bin").read_bytes(), image)
            self.assertEqual(json.loads((output / "release.json").read_text()), manifest)
            self.assertEqual(
                (output / "SHA256SUMS").read_text(),
                manifest["sha256"] + "  firmware.bin\n",
            )

    def test_wrong_embedded_version_is_rejected(self) -> None:
        with self.assertRaises(ota_upload.OtaUploadError):
            release_artifact.make_manifest("v0.10.2", sample_image(), "a" * 40)

    def test_release_manifest_validation(self) -> None:
        manifest = release_artifact.make_manifest("v0.10.1", sample_image(), "a" * 40)
        self.assertEqual(
            ota_upload.validate_release_manifest(manifest, "0.10.1"),
            (manifest["sha256"], manifest["size"]),
        )
        for field, bad_value in (("version", "0.10.2"), ("sha256", "bad"), ("size", True)):
            changed = dict(manifest)
            changed[field] = bad_value
            with self.subTest(field=field), self.assertRaises(ota_upload.OtaUploadError):
                ota_upload.validate_release_manifest(changed, "0.10.1")

    def test_release_download_checks_manifest_image_and_embedded_version(self) -> None:
        image = sample_image()
        manifest = release_artifact.make_manifest("v0.10.1", image, "a" * 40)
        url = "https://example.com/releases/download/v0.10.1/firmware.bin"
        responses = [io.BytesIO(json.dumps(manifest).encode("utf-8")), io.BytesIO(image)]
        with mock.patch.object(ota_upload.urllib.request, "urlopen", side_effect=responses):
            verified_url = ota_upload.verify_release_artifact(url, "0.10.1")
        self.assertIn(manifest["sha256"][:16], verified_url)

        responses = [io.BytesIO(json.dumps(manifest).encode("utf-8")), io.BytesIO(image + b"x")]
        with mock.patch.object(ota_upload.urllib.request, "urlopen", side_effect=responses):
            with self.assertRaises(ota_upload.OtaUploadError):
                ota_upload.verify_release_artifact(url, "0.10.1")

    def test_release_url_requires_exact_binary_name(self) -> None:
        with self.assertRaises(ota_upload.OtaUploadError):
            ota_upload.release_manifest_url("https://example.com/v0.10.1/other.bin")
        with self.assertRaises(ota_upload.OtaUploadError):
            ota_upload.release_manifest_url("https://example.com/v0.10.1/firmware.bin?x=1")


if __name__ == "__main__":
    unittest.main()
