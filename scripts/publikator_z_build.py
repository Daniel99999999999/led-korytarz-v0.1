#!/usr/bin/env python3
"""Publish the already-built OTA image using the project's existing safe release code.

Copy into scripts/ in led-korytarz-v0.1. Does NOT rebuild or flash over USB.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import time
import sys
import urllib.error
import urllib.request
from pathlib import Path

from ota_upload import (
    OtaUploadError,
    firmware_url_for_version,
    run_ota,
    verify_release_artifact,
)
from publish_ota import (
    RELEASE_REPO,
    check_source,
    load_ota_config,
    publish_release,
    push_tag,
)
from release_artifact import (
    OTA_PROFILE,
    VERSION_PATTERN,
    make_manifest,
    validate_source_version,
    write_release_assets,
)

ROOT = Path(__file__).resolve().parent.parent
IMAGE = ROOT / '.pio' / 'build' / OTA_PROFILE / 'firmware.bin'
RELEASE_DIR = ROOT / '.pio' / 'release'
CONFIG_FILE = ROOT / 'secrets.ota.json'
PREPARE_STAMP = ROOT / '.pio' / 'publikator_przygotowana_wersja.json'
SEMVER = re.compile(r'(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\Z')
TAG_SEMVER = re.compile(r'v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\Z')
CMAKE_VERSION_BYTES = re.compile(rb'(?m)^(set\(PROJECT_VER ")([^"\r\n]+)("\)[ \t]*\r?)$')


def semver(value: str) -> tuple[int, int, int]:
    match = SEMVER.fullmatch(value)
    if not match:
        raise OtaUploadError('Wersja musi miec format MAJOR.MINOR.PATCH, np. 0.10.4')
    return tuple(map(int, match.groups()))


def newest_published_version() -> str | None:
    """Look at actual GitHub Releases, not arbitrary version numbers in documentation."""
    url = 'https://api.github.com/repos/%s/releases?per_page=100' % RELEASE_REPO
    request = urllib.request.Request(url, headers={
        'Accept': 'application/vnd.github+json',
        'User-Agent': 'sterownik-led-publikator-wersji',
    })
    try:
        with urllib.request.urlopen(request, timeout=20) as response:
            releases = json.load(response)
    except (OSError, ValueError) as error:
        raise OtaUploadError(
            'Nie moge odczytac wersji z GitHub Releases. '
            'Nie zmieniam PROJECT_VER. Sprawdz polaczenie internetowe.'
        ) from error
    if not isinstance(releases, list):
        raise OtaUploadError('GitHub zwrocil nieoczekiwana liste wydan; bez zmian')
    if len(releases) >= 100:
        raise OtaUploadError('GitHub ma >=100 wydan; lista jest niepelna. Bez zmian.')
    versions = []
    for release in releases:
        if not isinstance(release, dict) or release.get('draft'):
            continue
        match = TAG_SEMVER.fullmatch(str(release.get('tag_name', '')))
        if match:
            versions.append(tuple(map(int, match.groups())))
    return '.'.join(map(str, max(versions))) if versions else None


def write_project_version(new_version: str) -> bool:
    """Change only the PROJECT_VER assignment, preserving line endings and rest of file."""
    cmake_path = ROOT / 'CMakeLists.txt'
    original = cmake_path.read_bytes()
    matches = list(CMAKE_VERSION_BYTES.finditer(original))
    if len(matches) != 1:
        raise OtaUploadError('Oczekiwalem jednej linii set(PROJECT_VER "...") w CMakeLists.txt')
    old = matches[0].group(2).decode('ascii')
    semver(old)
    semver(new_version)
    if old == new_version:
        return False
    start, end = matches[0].span(2)
    changed = original[:start] + new_version.encode('ascii') + original[end:]
    temp = cmake_path.with_name('.CMakeLists.txt.publikator.tmp')
    try:
        temp.write_bytes(changed)
        os.replace(temp, cmake_path)
    finally:
        if temp.exists():
            temp.unlink()
    return True


def prepare_version(explicit_version: str | None, yes: bool) -> None:
    """Run BEFORE compiling to put the correct version in the ESP-IDF binary."""
    current = current_version()
    newest = newest_published_version()
    recommended = current
    if newest is not None and semver(current) <= semver(newest):
        major, minor, patch = semver(newest)
        recommended = '%d.%d.%d' % (major, minor, patch + 1)
    print('PUBLIKATOR: wersja zrodel (CMakeLists.txt): %s' % current)
    print('PUBLIKATOR: najwyzsza wersja GitHub Releases: %s' % (newest or '(brak)'))
    print('PUBLIKATOR: proponowana wersja: %s' % recommended)
    if explicit_version is not None:
        selected = explicit_version.strip()
    elif yes:
        selected = recommended
    else:
        selected = input('Nowa wersja [Enter = %s, Q = anuluj]: ' % recommended).strip()
        if selected.lower() == 'q':
            print('Anulowano; CMakeLists.txt niezmieniony.')
            return
        selected = selected or recommended
    selected_tuple = semver(selected)
    if newest and selected_tuple <= semver(newest):
        raise OtaUploadError('Wersja %s jest juz opublikowana lub starsza od %s' % (selected, newest))
    if selected_tuple < semver(current):
        raise OtaUploadError('Nowa wersja %s jest starsza od zrodlowej %s' % (selected, current))
    if not yes:
        confirm = input('Zapisac PROJECT_VER = %s? [t/N]: ' % selected).strip().lower()
        if confirm not in ('t', 'tak', 'y', 'yes'):
            print('Anulowano; CMakeLists.txt niezmieniony.')
            return
    changed = write_project_version(selected)
    print(('Zmieniono' if changed else 'Pozostawiono') + ' PROJECT_VER = %s' % selected)
    PREPARE_STAMP.parent.mkdir(parents=True, exist_ok=True)
    PREPARE_STAMP.write_text(json.dumps({
        'version': selected,
        'cmake_sha256': hashlib.sha256((ROOT / 'CMakeLists.txt').read_bytes()).hexdigest(),
        'prepared_at_ns': time.time_ns(),
    }, ensure_ascii=True, indent=2) + '\n', encoding='utf-8')
    print('TERAZ: wykonaj BUILD w VS Code, w profilu esp32doit-devkit-v1-ota.')
    print('NASTEPNIE: Commit i Push zrodel, uruchom publikator i wybierz [1] lub [2].')


def check_prepared_version_build(version: str) -> None:
    """Prevent publishing a binary built before the version was prepared."""
    if not PREPARE_STAMP.is_file():
        return  # Supports projects prepared manually before installing this tool.
    try:
        prepared = json.loads(PREPARE_STAMP.read_text(encoding='utf-8'))
        prepared_version = prepared['version']
        prepared_ns = int(prepared['prepared_at_ns'])
        cmake_sha = prepared['cmake_sha256']
    except (OSError, ValueError, KeyError, TypeError) as error:
        raise OtaUploadError('Niepoprawny znacznik przygotowanej wersji; przygotuj ja ponownie') from error
    if prepared_version != version:
        raise OtaUploadError('Od przygotowania wersji zmieniono PROJECT_VER. Uruchom [0] ponownie.')
    if hashlib.sha256((ROOT / 'CMakeLists.txt').read_bytes()).hexdigest() != cmake_sha:
        raise OtaUploadError('Zmieniono CMakeLists.txt po przygotowaniu wersji. Uruchom [0] i Build.')
    if IMAGE.is_file() and IMAGE.stat().st_mtime_ns <= prepared_ns:
        raise OtaUploadError('Firmware.bin jest starszy od przygotowania wersji. Uruchom Build OTA.')


def current_version() -> str:
    cmake = ROOT / 'CMakeLists.txt'
    matches = VERSION_PATTERN.findall(cmake.read_text(encoding='utf-8'))
    if len(matches) != 1:
        raise OtaUploadError('Nie znaleziono jednej wersji PROJECT_VER w CMakeLists.txt')
    return validate_source_version('v' + matches[0], cmake)


def ensure_build_not_stale(image_path: Path) -> None:
    """Guard against editing source AFTER the build (conservative filesystem check)."""
    if not image_path.is_file():
        raise OtaUploadError(
            'Brak skompilowanego firmware.bin w profilu %s. W VS Code wykonaj Build.'
            % OTA_PROFILE
        )
    build_time = image_path.stat().st_mtime
    candidates = [
        ROOT / 'CMakeLists.txt', ROOT / 'platformio.ini',
        ROOT / 'sdkconfig.esp32doit-devkit-v1',
        ROOT / 'partitions_ota_4mb.csv',
    ]
    for folder in ('src', 'include'):
        base = ROOT / folder
        if base.exists():
            candidates.extend(
                p for p in base.rglob('*')
                if p.is_file() and p.suffix.lower() in ('.c', '.cpp', '.h', '.hpp', '.yml', '.txt')
            )
    newer = [p for p in candidates if p.is_file() and p.stat().st_mtime > build_time + 2]
    if newer:
        examples = ', '.join(str(p.relative_to(ROOT)) for p in newer[:3])
        raise OtaUploadError(
            'Pliki projektu sa nowsze niz firmware.bin (%s). Uruchom ponownie Build OTA.'
            % examples
        )


def release_config() -> dict:
    """No MQTT credentials required for upload-only mode."""
    return {'firmware_repo': RELEASE_REPO, 'artifact_source': 'release', 'publish_path': None}


def mqtt_config(host: str | None) -> dict:
    return load_ota_config(CONFIG_FILE, host)


def fetch_public_manifest(firmware_url: str) -> dict:
    url = firmware_url.rsplit('/', 1)[0] + '/release.json'
    try:
        with urllib.request.urlopen(url, timeout=30) as response:
            data = json.load(response)
        if not isinstance(data, dict):
            raise ValueError('not a JSON object')
        return data
    except (OSError, ValueError) as error:
        raise OtaUploadError('Nie mozna odczytac manifestu wydania z GitHub') from error


def publish_existing_build(version: str, with_install: bool, host: str | None) -> None:
    tag = 'v' + version
    print('PUBLIKATOR: profil = %s, wersja = %s' % (OTA_PROFILE, tag), flush=True)
    # Protect release provenance: clean main branch, pushed to origin, matching tag.
    commit = check_source(tag)
    ensure_build_not_stale(IMAGE)
    check_prepared_version_build(version)
    image = IMAGE.read_bytes()
    manifest = make_manifest(tag, image, commit)  # validates header, project, version, OTA size
    print('PUBLIKATOR: firmware.bin = %u bajtow; SHA256 = %s' % (
        len(image), manifest['sha256']), flush=True)
    config = mqtt_config(host) if with_install else release_config()
    write_release_assets(IMAGE, RELEASE_DIR, manifest)
    firmware_url = publish_release(tag, commit, RELEASE_DIR, config)

    # An existing release is immutable for safety. Never silently install a DIFFERENT
    # public binary when a rebuilt local binary shares the same version/commit.
    public_manifest = fetch_public_manifest(firmware_url)
    if public_manifest.get('sha256') != manifest['sha256']:
        raise OtaUploadError(
            'Wydanie %s juz istnieje, ale SHA256 publicznego pliku jest inne! '
            'Nie nadpisuje wydania i nie uruchamiam OTA. '
            'Wybierz osobno instalacje istniejacego wydania albo zwieksz wersje.' % tag
        )
    push_tag(tag, commit)
    print('PUBLIKATOR: wydanie opublikowane i zweryfikowane: %s' % firmware_url, flush=True)
    if with_install:
        print('PUBLIKATOR: wysylam polecenie OTA przez MQTT...', flush=True)
        run_ota(config, firmware_url, version)
        print('PUBLIKATOR: sterownik potwierdzil nowa wersje.', flush=True)
    else:
        print('PUBLIKATOR: gotowe. Nie wysylano polecenia OTA.', flush=True)


def existing_release(version: str, host: str | None) -> tuple[dict, str]:
    config = mqtt_config(host)
    url = firmware_url_for_version(config, version)
    verified_url = verify_release_artifact(url, version)
    return config, verified_url


def install_existing(version: str, host: str | None) -> None:
    config, url = existing_release(version, host)
    print('PUBLIKATOR: potwierdzono opublikowane wydanie: %s' % url, flush=True)
    print('PUBLIKATOR: wysylam polecenie MQTT...', flush=True)
    run_ota(config, url, version)
    print('PUBLIKATOR: instalacja potwierdzona.', flush=True)


def make_ha_yaml(version: str, host: str | None) -> None:
    config, url = existing_release(version, host)
    topic_prefix = str(config.get('topic_prefix', '')).strip('/')
    device_id = str(config.get('device_id', ''))
    if (not topic_prefix or not device_id or any(x in topic_prefix for x in '+#')
            or any(x in device_id for x in '/+#')):
        raise OtaUploadError('Brak poprawnego topic_prefix / device_id w secrets.ota.json')
    topic = '%s/%s/ota/set' % (topic_prefix, device_id)
    RELEASE_DIR.mkdir(parents=True, exist_ok=True)
    output = RELEASE_DIR / ('HA_OTA_v%s.yaml' % version)
    yaml_text = (
        'action: mqtt.publish\n'
        'data:\n'
        '  topic: ' + json.dumps(topic) + '\n'
        '  payload: ' + json.dumps(url) + '\n'
        '  qos: 1\n'
        '  retain: false\n'
    )
    output.write_text(yaml_text, encoding='utf-8')
    print('PUBLIKATOR: gotowy YAML do Narzedzia deweloperskie > Akcje:')
    print(yaml_text)
    print('Zapisano: %s' % output)
    print('UWAGA: MQTT publish nie monitoruje sukcesu instalacji; sprawdz sensor OTA.')


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    choices = ('prepare-version', 'publish-only', 'publish-install', 'install-only', 'ha-yaml')
    parser.add_argument('operation', choices=choices)
    parser.add_argument('--mqtt-host', default=None, help='Opcjonalnie nowy adres brokera MQTT')
    parser.add_argument('--version', default=None, help='Recznie wskazana nowa wersja dla prepare-version')
    parser.add_argument('--yes', action='store_true', help='W trybie prepare-version zastosuj bez potwierdzenia')
    args = parser.parse_args()
    if args.operation == 'prepare-version':
        prepare_version(args.version, args.yes)
        return 0
    version = current_version()
    if args.operation == 'publish-only':
        publish_existing_build(version, False, None)
    elif args.operation == 'publish-install':
        publish_existing_build(version, True, args.mqtt_host)
    elif args.operation == 'install-only':
        install_existing(version, args.mqtt_host)
    else:
        make_ha_yaml(version, args.mqtt_host)
    return 0


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (OtaUploadError, OSError, ValueError, urllib.error.URLError) as error:
        print('BLAD PUBLIKATORA: %s' % error, file=sys.stderr)
        raise SystemExit(1)
