#!/usr/bin/env python3

"""PlatformIO uploader for the firmware's MQTT-triggered HTTPS OTA path."""

from __future__ import annotations

import hashlib
import json
import os
import re
import secrets
import shutil
import socket
import ssl
import struct
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple


CONFIG_NAME = "secrets.ota.json"
EXPECTED_PROJECT = "led_controller"
APP_DESCRIPTOR_MAGIC = b"\x32\x54\xCD\xAB"
MAX_OTA_URL_LENGTH = 511
MQTT_KEEPALIVE_SECONDS = 30


class OtaUploadError(RuntimeError):
    pass


def require_string(config: Dict[str, Any], key: str, allow_empty: bool = False) -> str:
    value = config.get(key)
    if not isinstance(value, str) or (not allow_empty and not value.strip()):
        raise OtaUploadError("Pole %s musi byc niepustym tekstem" % key)
    return value


def parse_app_descriptor(image: bytes) -> Tuple[str, str]:
    offset = image.find(APP_DESCRIPTOR_MAGIC)
    while offset >= 0:
        if offset + 80 <= len(image):
            version = image[offset + 16 : offset + 48].split(b"\0", 1)[0]
            project = image[offset + 48 : offset + 80].split(b"\0", 1)[0]
            try:
                decoded_version = version.decode("ascii")
                decoded_project = project.decode("ascii")
            except UnicodeDecodeError:
                decoded_version = ""
                decoded_project = ""
            if decoded_project == EXPECTED_PROJECT and decoded_version:
                return decoded_project, decoded_version
        offset = image.find(APP_DESCRIPTOR_MAGIC, offset + 1)
    raise OtaUploadError("Nie znaleziono poprawnego deskryptora aplikacji ESP-IDF")


def validate_ota_url(url: str) -> None:
    try:
        url.encode("ascii")
    except UnicodeEncodeError as error:
        raise OtaUploadError("URL OTA musi zawierac drukowalne ASCII") from error
    parsed = urllib.parse.urlsplit(url)
    if parsed.scheme.lower() != "https" or not parsed.hostname:
        raise OtaUploadError("firmware_url musi byc pelnym adresem https://")
    if parsed.username is not None or parsed.password is not None or parsed.fragment:
        raise OtaUploadError("URL OTA nie moze zawierac danych logowania ani fragmentu")
    if len(url) > MAX_OTA_URL_LENGTH:
        raise OtaUploadError("URL OTA przekracza limit %u bajtow" % MAX_OTA_URL_LENGTH)


def firmware_url_for_version(config: Dict[str, Any], version: str) -> str:
    repo = config.get("firmware_repo")
    if repo is not None:
        if "firmware_url" in config:
            raise OtaUploadError("Uzyj firmware_repo albo firmware_url, nie obu pol")
        if not isinstance(repo, str) or re.fullmatch(
            r"[A-Za-z0-9-]+/[A-Za-z0-9_.-]+", repo
        ) is None:
            raise OtaUploadError("firmware_repo musi miec postac owner/repo")
        if re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", version) is None:
            raise OtaUploadError("Wersja obrazu nie nadaje sie do tagu Release")
        if config.get("artifact_source") != "release":
            raise OtaUploadError("firmware_repo wymaga artifact_source: release")
        url = "https://github.com/%s/releases/download/v%s/firmware.bin" % (
            repo, version
        )
    else:
        url = require_string(config, "firmware_url")
    validate_ota_url(url)
    return url


def publish_local_artifact(source: Path, configured_target: Any) -> None:
    if configured_target is None:
        return
    if not isinstance(configured_target, str) or not configured_target.strip():
        raise OtaUploadError("publish_path musi byc sciezka albo wartoscia null")

    target = Path(os.path.expandvars(configured_target)).expanduser().resolve()
    if target == source:
        return
    target.parent.mkdir(parents=True, exist_ok=True)
    temporary = target.with_name(target.name + ".pio-ota.tmp")
    try:
        shutil.copyfile(source, temporary)
        os.replace(temporary, target)
    finally:
        if temporary.exists():
            temporary.unlink()
    print("OTA: opublikowano lokalny artefakt: %s" % target)


def add_cache_buster(url: str, digest: str) -> str:
    separator = "&" if urllib.parse.urlsplit(url).query else "?"
    candidate = "%s%spio_sha256=%s" % (url, separator, digest[:16])
    validate_ota_url(candidate)
    return candidate


def verify_remote_artifact(url: str, expected_digest: str, expected_size: int) -> bytes:
    request = urllib.request.Request(
        url,
        headers={"Cache-Control": "no-cache", "User-Agent": "PlatformIO-sterownik-led-OTA/1"},
    )
    digest = hashlib.sha256()
    size = 0
    header = bytearray()
    try:
        with urllib.request.urlopen(request, timeout=30) as response:
            while True:
                chunk = response.read(64 * 1024)
                if not chunk:
                    break
                digest.update(chunk)
                size += len(chunk)
                if len(header) < 512:
                    header.extend(chunk[: 512 - len(header)])
    except (OSError, urllib.error.URLError) as error:
        raise OtaUploadError("Nie mozna pobrac firmware_url: %s" % error) from error

    if size != expected_size or digest.hexdigest() != expected_digest:
        raise OtaUploadError(
            "Serwer HTTPS nie udostepnia oczekiwanego firmware.bin "
            "(oczekiwano %u B, zdalnie %u B)" % (expected_size, size)
        )
    print("OTA: rozmiar i SHA-256 pliku na serwerze sa zgodne")
    return bytes(header)


def release_manifest_url(firmware_url: str) -> str:
    parsed = urllib.parse.urlsplit(firmware_url)
    if not parsed.path.endswith("/firmware.bin") or parsed.query:
        raise OtaUploadError(
            "Tryb release wymaga URL konczacego sie na /firmware.bin bez parametrow"
        )
    url = urllib.parse.urlunsplit(
        parsed._replace(path=parsed.path[: -len("firmware.bin")] + "release.json")
    )
    validate_ota_url(url)
    return url


def validate_release_manifest(manifest: Any, local_version: str) -> Tuple[str, int]:
    if not isinstance(manifest, dict):
        raise OtaUploadError("release.json musi byc obiektem JSON")
    expected = {
        "schema": 1,
        "project": EXPECTED_PROJECT,
        "version": local_version,
        "tag": "v" + local_version,
        "profile": "esp32doit-devkit-v1-ota",
        "filename": "firmware.bin",
    }
    if any(type(manifest.get(key)) is not type(value) or manifest.get(key) != value
           for key, value in expected.items()):
        raise OtaUploadError("release.json nie zgadza sie z lokalna wersja lub profilem OTA")
    size = manifest.get("size")
    digest = manifest.get("sha256")
    commit = manifest.get("source_commit")
    if type(size) is not int or not 0 < size <= 0x1D0000:
        raise OtaUploadError("Niepoprawny rozmiar obrazu w release.json")
    if not isinstance(digest, str) or re.fullmatch(r"[0-9a-f]{64}", digest) is None:
        raise OtaUploadError("Niepoprawny SHA-256 w release.json")
    if not isinstance(commit, str) or re.fullmatch(r"[0-9a-f]{40}", commit) is None:
        raise OtaUploadError("Niepoprawny commit zrodel w release.json")
    return digest, size


def verify_release_artifact(firmware_url: str, local_version: str) -> str:
    manifest_url = release_manifest_url(firmware_url)
    request = urllib.request.Request(
        manifest_url,
        headers={"Cache-Control": "no-cache", "User-Agent": "PlatformIO-sterownik-led-OTA/1"},
    )
    try:
        with urllib.request.urlopen(request, timeout=30) as response:
            data = response.read(4097)
        if len(data) > 4096:
            raise OtaUploadError("release.json przekracza limit 4096 B")
        manifest = json.loads(data.decode("utf-8"))
    except (OSError, urllib.error.URLError, UnicodeDecodeError, json.JSONDecodeError) as error:
        raise OtaUploadError("Nie mozna odczytac release.json: %s" % error) from error
    digest, size = validate_release_manifest(manifest, local_version)
    ota_url = add_cache_buster(firmware_url, digest)
    header = verify_remote_artifact(ota_url, digest, size)
    _, version = parse_app_descriptor(header)
    if version != local_version:
        raise OtaUploadError("Wersja opublikowanego obrazu rozni sie od lokalnej wersji")
    print("OTA: sprawdzono Release %s, commit %s" % (
        manifest["tag"], manifest["source_commit"]
    ))
    return ota_url


def encode_mqtt_length(value: int) -> bytes:
    if value < 0 or value > 268435455:
        raise OtaUploadError("Niepoprawna dlugosc pakietu MQTT")
    output = bytearray()
    while True:
        encoded = value % 128
        value //= 128
        if value:
            encoded |= 0x80
        output.append(encoded)
        if not value:
            return bytes(output)


def encode_mqtt_string(value: str) -> bytes:
    encoded = value.encode("utf-8")
    if len(encoded) > 65535:
        raise OtaUploadError("Pole MQTT jest za dlugie")
    return struct.pack("!H", len(encoded)) + encoded


class MinimalMqttClient:
    def __init__(self, config: Dict[str, Any]) -> None:
        self.host = require_string(config, "mqtt_host")
        port = config.get("mqtt_port", 1883)
        if not isinstance(port, int) or not 1 <= port <= 65535:
            raise OtaUploadError("mqtt_port musi byc liczba 1..65535")
        self.port = port
        self.username = require_string(config, "mqtt_username", allow_empty=True)
        self.password = require_string(config, "mqtt_password", allow_empty=True)
        self.use_tls = config.get("mqtt_tls", False)
        if not isinstance(self.use_tls, bool):
            raise OtaUploadError("mqtt_tls musi miec wartosc true albo false")
        self.ca_file = config.get("mqtt_ca_file")
        if self.ca_file is not None and not isinstance(self.ca_file, str):
            raise OtaUploadError("mqtt_ca_file musi byc sciezka albo wartoscia null")
        self.sock: Optional[socket.socket] = None
        self.pending_messages: List[Tuple[str, bytes]] = []
        self.last_network_activity = time.monotonic()

    def connect(self) -> None:
        try:
            raw_socket = socket.create_connection((self.host, self.port), timeout=10)
            if self.use_tls:
                context = ssl.create_default_context(cafile=self.ca_file)
                self.sock = context.wrap_socket(raw_socket, server_hostname=self.host)
            else:
                self.sock = raw_socket
        except OSError as error:
            raise OtaUploadError("Polaczenie z brokerem MQTT nie powiodlo sie: %s" % error) from error

        client_id = "pio-ota-%d-%s" % (os.getpid(), secrets.token_hex(3))
        flags = 0x02
        payload = encode_mqtt_string(client_id)
        if self.username:
            flags |= 0x80
            payload += encode_mqtt_string(self.username)
        if self.password:
            flags |= 0x40
            payload += encode_mqtt_string(self.password)
        variable_header = (
            encode_mqtt_string("MQTT")
            + b"\x04"
            + bytes((flags,))
            + struct.pack("!H", MQTT_KEEPALIVE_SECONDS)
        )
        self._send(0x10, variable_header + payload)
        header, body = self._receive(10)
        if (header >> 4) != 2 or len(body) != 2:
            raise OtaUploadError("Broker nie wyslal poprawnego CONNACK")
        if body[1] != 0:
            raise OtaUploadError("Broker MQTT odrzucil polaczenie, kod CONNACK=%u" % body[1])
        print("OTA: polaczono z brokerem MQTT %s:%u" % (self.host, self.port))

    def close(self) -> None:
        if self.sock is None:
            return
        try:
            self._send(0xE0, b"")
        except OtaUploadError:
            pass
        try:
            self.sock.close()
        finally:
            self.sock = None

    def subscribe(self, topic: str, packet_id: int) -> None:
        self._send(0x82, struct.pack("!H", packet_id) + encode_mqtt_string(topic) + b"\x01")
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            try:
                header, body = self._receive(max(0.1, deadline - time.monotonic()))
            except socket.timeout:
                continue
            packet_type = header >> 4
            if packet_type == 9 and len(body) >= 3 and struct.unpack("!H", body[:2])[0] == packet_id:
                if body[2] == 0x80:
                    raise OtaUploadError("Broker odrzucil subskrypcje statusu OTA")
                return
            self._handle_packet(header, body)
        raise OtaUploadError("Timeout subskrypcji statusu OTA")

    def publish_qos1(self, topic: str, payload: bytes, packet_id: int) -> None:
        body = encode_mqtt_string(topic) + struct.pack("!H", packet_id) + payload
        self._send(0x32, body)
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            try:
                header, response = self._receive(max(0.1, deadline - time.monotonic()))
            except socket.timeout:
                continue
            packet_type = header >> 4
            if (
                packet_type == 4
                and len(response) == 2
                and struct.unpack("!H", response)[0] == packet_id
            ):
                return
            self._handle_packet(header, response)
        raise OtaUploadError("Broker nie potwierdzil komendy OTA")

    def poll(self, timeout: float) -> Optional[Tuple[str, bytes]]:
        if self.pending_messages:
            return self.pending_messages.pop(0)
        try:
            header, body = self._receive(timeout)
        except socket.timeout:
            if time.monotonic() - self.last_network_activity >= MQTT_KEEPALIVE_SECONDS / 2:
                self._send(0xC0, b"")
            return None
        self._handle_packet(header, body)
        return self.pending_messages.pop(0) if self.pending_messages else None

    def _handle_packet(self, header: int, body: bytes) -> None:
        packet_type = header >> 4
        if packet_type != 3:
            return
        if len(body) < 2:
            raise OtaUploadError("Uszkodzony PUBLISH MQTT")
        topic_length = struct.unpack("!H", body[:2])[0]
        offset = 2 + topic_length
        if offset > len(body):
            raise OtaUploadError("Uszkodzony temat PUBLISH MQTT")
        topic = body[2:offset].decode("utf-8")
        qos = (header >> 1) & 0x03
        packet_id = 0
        if qos:
            if offset + 2 > len(body):
                raise OtaUploadError("Brak identyfikatora PUBLISH MQTT")
            packet_id = struct.unpack("!H", body[offset : offset + 2])[0]
            offset += 2
        self.pending_messages.append((topic, body[offset:]))
        if qos == 1:
            self._send(0x40, struct.pack("!H", packet_id))

    def _send(self, header: int, body: bytes) -> None:
        if self.sock is None:
            raise OtaUploadError("Brak polaczenia MQTT")
        try:
            self.sock.sendall(bytes((header,)) + encode_mqtt_length(len(body)) + body)
            self.last_network_activity = time.monotonic()
        except OSError as error:
            raise OtaUploadError("Blad zapisu MQTT: %s" % error) from error

    def _receive(self, timeout: float) -> Tuple[int, bytes]:
        if self.sock is None:
            raise OtaUploadError("Brak polaczenia MQTT")
        self.sock.settimeout(timeout)
        header = self._read_exact(1)[0]
        multiplier = 1
        remaining = 0
        for _ in range(4):
            encoded = self._read_exact(1)[0]
            remaining += (encoded & 0x7F) * multiplier
            if not encoded & 0x80:
                body = self._read_exact(remaining)
                self.last_network_activity = time.monotonic()
                return header, body
            multiplier *= 128
        raise OtaUploadError("Niepoprawna dlugosc odebranego pakietu MQTT")

    def _read_exact(self, size: int) -> bytes:
        if self.sock is None:
            raise OtaUploadError("Brak polaczenia MQTT")
        output = bytearray()
        while len(output) < size:
            try:
                chunk = self.sock.recv(size - len(output))
            except socket.timeout:
                raise
            except OSError as error:
                raise OtaUploadError("Blad odczytu MQTT: %s" % error) from error
            if not chunk:
                raise OtaUploadError("Broker MQTT zamknal polaczenie")
            output.extend(chunk)
        return bytes(output)


def decode_status(payload: bytes) -> Optional[Dict[str, Any]]:
    try:
        status = json.loads(payload.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError):
        return None
    return status if isinstance(status, dict) else None


def run_ota(config: Dict[str, Any], url: str, candidate_version: str) -> None:
    prefix = require_string(config, "topic_prefix").strip("/")
    device_id = require_string(config, "device_id")
    if not prefix or any(character in prefix for character in "+#"):
        raise OtaUploadError("topic_prefix jest niepoprawny")
    if any(character in device_id for character in "/+#"):
        raise OtaUploadError("device_id jest niepoprawny")
    timeout = config.get("timeout_seconds", 180)
    if not isinstance(timeout, int) or not 30 <= timeout <= 600:
        raise OtaUploadError("timeout_seconds musi byc liczba 30..600")

    command_topic = "%s/%s/ota/set" % (prefix, device_id)
    status_topic = "%s/%s/ota/status" % (prefix, device_id)
    client = MinimalMqttClient(config)
    try:
        client.connect()
        client.subscribe(status_topic, 1)

        # Remove the retained status observed before issuing this particular command.
        drain_deadline = time.monotonic() + 0.5
        while time.monotonic() < drain_deadline:
            client.poll(max(0.01, drain_deadline - time.monotonic()))

        client.publish_qos1(command_topic, url.encode("ascii"), 2)
        print("OTA: komenda przyjeta przez broker; oczekiwanie na sterownik")

        deadline = time.monotonic() + timeout
        last_report: Optional[Tuple[Any, ...]] = None
        update_seen = False
        while time.monotonic() < deadline:
            message = client.poll(min(1.0, max(0.01, deadline - time.monotonic())))
            if message is None or message[0] != status_topic:
                continue
            status = decode_status(message[1])
            if status is None:
                continue
            state = status.get("state")
            version = status.get("version")
            candidate = status.get("candidate")
            progress = status.get("progress")
            error = status.get("error")
            report = (state, version, candidate, progress, error)
            if report != last_report:
                print(
                    "OTA: stan=%s, wersja=%s, kandydat=%s, postep=%s%%, blad=%s"
                    % (state, version, candidate or "-", progress, error)
                )
                last_report = report

            if state in ("queued", "downloading", "verifying", "rebooting"):
                update_seen = True
            if update_seen and state == "failed":
                raise OtaUploadError("Sterownik odrzucil albo przerwal OTA: %s" % error)
            if update_seen and version == candidate_version and state == "idle":
                print("OTA: wersja %s uruchomiona i zatwierdzona" % candidate_version)
                return
        raise OtaUploadError("Timeout oczekiwania na uruchomienie i zatwierdzenie nowej wersji")
    finally:
        client.close()


def main() -> int:
    if len(sys.argv) != 2:
        raise OtaUploadError("Uzycie: ota_upload.py <firmware.bin>")
    firmware_path = Path(sys.argv[1]).resolve()
    if not firmware_path.is_file():
        raise OtaUploadError("Nie znaleziono firmware.bin: %s" % firmware_path)

    project_dir = Path(__file__).resolve().parent.parent
    config_path = project_dir / CONFIG_NAME
    if not config_path.is_file():
        raise OtaUploadError(
            "Brak %s. Uzupelnij lokalny plik na podstawie secrets.example.ota.json"
            % config_path
        )
    try:
        config = json.loads(config_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise OtaUploadError("Nie mozna odczytac %s: %s" % (config_path, error)) from error
    if not isinstance(config, dict):
        raise OtaUploadError("Glowna wartosc konfiguracji OTA musi byc obiektem JSON")

    image = firmware_path.read_bytes()
    project, version = parse_app_descriptor(image)
    digest = hashlib.sha256(image).hexdigest()
    print("OTA: obraz %s, wersja %s, %u B" % (project, version, len(image)))

    base_url = firmware_url_for_version(config, version)
    artifact_source = config.get("artifact_source", "local")
    if artifact_source == "release":
        if config.get("publish_path") is not None:
            raise OtaUploadError("Tryb release wymaga publish_path: null")
        ota_url = verify_release_artifact(base_url, version)
    elif artifact_source == "local":
        publish_local_artifact(firmware_path, config.get("publish_path"))
        ota_url = add_cache_buster(base_url, digest)
        verify_remote_artifact(ota_url, digest, len(image))
    else:
        raise OtaUploadError("artifact_source musi miec wartosc local albo release")
    run_ota(config, ota_url, version)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except OtaUploadError as error:
        print("ERROR OTA: %s" % error, file=sys.stderr)
        sys.exit(1)
