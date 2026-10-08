#!/usr/bin/env python3

"""Host tests for tagged release packaging and release-based OTA validation."""

import io
import json
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import ota_upload
import publish_ota
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
    def test_local_config_migration_preserves_mqtt_credentials(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "secrets.ota.json"
            path.write_text(json.dumps({
                "firmware_url": "https://example.com/old.bin",
                "mqtt_host": "192.168.1.40",
                "mqtt_username": "user",
                "mqtt_password": "secret",
            }), encoding="utf-8")
            config = publish_ota.load_ota_config(path, "192.168.1.130")
            self.assertEqual(config["mqtt_host"], "192.168.1.130")
            self.assertEqual(config["mqtt_password"], "secret")
            self.assertNotIn("firmware_url", config)
            self.assertEqual(config["firmware_repo"], publish_ota.RELEASE_REPO)
            self.assertEqual(json.loads(path.read_text())["mqtt_password"], "secret")

    def test_release_url_follows_binary_version_without_config_edit(self) -> None:
        config = {
            "firmware_repo": "Daniel99999999999/led-korytarz-firmware",
            "artifact_source": "release",
        }
        self.assertEqual(
            ota_upload.firmware_url_for_version(config, "0.10.3"),
            "https://github.com/Daniel99999999999/led-korytarz-firmware/"
            "releases/download/v0.10.3/firmware.bin",
        )
        self.assertIn("v0.10.4", ota_upload.firmware_url_for_version(config, "0.10.4"))
        for changed in (
            {**config, "firmware_url": "https://example.com/firmware.bin"},
            {**config, "firmware_repo": "not-a-repo"},
            {**config, "artifact_source": "local"},
        ):
            with self.assertRaises(ota_upload.OtaUploadError):
                ota_upload.firmware_url_for_version(changed, "0.10.3")

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

    def test_existing_release_is_verified_without_upload(self) -> None:
        config = {"firmware_repo": publish_ota.RELEASE_REPO, "artifact_source": "release"}
        with mock.patch.object(publish_ota, "github_credential", return_value="token"), \
             mock.patch.object(publish_ota, "github_request", return_value={"draft": False}) as api, \
             mock.patch.object(publish_ota, "verify_published_release", return_value="url"):
            self.assertEqual(
                publish_ota.publish_release("v0.10.3", "a" * 40, Path("unused"), config),
                "url",
            )
        self.assertEqual(api.call_count, 1)
        self.assertEqual(api.call_args.args[0], "GET")

    def test_public_release_rejects_other_source_commit(self) -> None:
        config = {"firmware_repo": publish_ota.RELEASE_REPO, "artifact_source": "release"}
        manifest = release_artifact.make_manifest("v0.10.1", sample_image(), "a" * 40)
        with mock.patch.object(
            publish_ota.urllib.request, "urlopen",
            return_value=io.BytesIO(json.dumps(manifest).encode("utf-8")),
        ):
            with self.assertRaises(ota_upload.OtaUploadError):
                publish_ota.verify_published_release("v0.10.1", "b" * 40, config)


if __name__ == "__main__":
    unittest.main()
