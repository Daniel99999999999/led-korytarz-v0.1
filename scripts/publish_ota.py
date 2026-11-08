#!/usr/bin/env python3

"""Build, publish and deploy one tagged firmware release from this PC."""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path
from typing import Any, Dict, Optional

from ota_upload import OtaUploadError, firmware_url_for_version, run_ota, verify_release_artifact, verify_remote_artifact
from release_artifact import (
    OTA_PROFILE,
    VERSION_PATTERN,
    make_manifest,
    validate_source_version,
    write_release_assets,
)


ROOT = Path(__file__).resolve().parent.parent
RELEASE_REPO = "Daniel99999999999/led-korytarz-firmware"
API_ROOT = "https://api.github.com/repos/" + RELEASE_REPO


def git(*args: str) -> str:
    result = subprocess.run(
        ["git", *args], cwd=ROOT, text=True, capture_output=True, check=False
    )
    if result.returncode:
        raise OtaUploadError("Git nie wykonal polecenia: git %s" % " ".join(args))
    return result.stdout.strip()


def check_source(tag: str) -> str:
    if git("status", "--porcelain"):
        raise OtaUploadError("Repozytorium ma niezapisane zmiany; najpierw commit i push")
    if git("symbolic-ref", "--short", "HEAD") != "main":
        raise OtaUploadError("Wydanie wymaga galezi main")
    commit = git("rev-parse", "HEAD")
    remote = git("ls-remote", "origin", "refs/heads/main").split()
    if not remote or remote[0] != commit:
        raise OtaUploadError("origin/main nie wskazuje aktualnego commitu; wykonaj push")

    local_tag = subprocess.run(
        ["git", "rev-parse", "-q", "--verify", "refs/tags/%s^{commit}" % tag],
        cwd=ROOT, text=True, capture_output=True, check=False,
    )
    if local_tag.returncode == 0 and local_tag.stdout.strip() != commit:
        raise OtaUploadError("Lokalny tag %s wskazuje inny commit" % tag)
    remote_tag = git("ls-remote", "origin", "refs/tags/%s^{}" % tag).split()
    if remote_tag and remote_tag[0] != commit:
        raise OtaUploadError("Zdalny tag %s wskazuje inny commit" % tag)
    if not remote_tag:
        remote_tag = git("ls-remote", "origin", "refs/tags/%s" % tag).split()
        if remote_tag and remote_tag[0] != commit:
            raise OtaUploadError("Zdalny tag %s wskazuje inny commit" % tag)
    return commit


def load_ota_config(path: Path, mqtt_host: Optional[str]) -> Dict[str, Any]:
    if not path.is_file():
        raise OtaUploadError("Brak secrets.ota.json; uzupelnij lokalnie dane MQTT")
    try:
        config = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise OtaUploadError("Nie mozna odczytac secrets.ota.json") from error
    if not isinstance(config, dict):
        raise OtaUploadError("secrets.ota.json musi zawierac obiekt JSON")
    if not config.get("mqtt_username") or not config.get("mqtt_password"):
        raise OtaUploadError("Brak lokalnych danych logowania MQTT")
    changed = (
        config.get("firmware_repo") != RELEASE_REPO
        or config.get("artifact_source") != "release"
        or config.get("publish_path") is not None
        or "firmware_url" in config
        or (mqtt_host is not None and config.get("mqtt_host") != mqtt_host)
    )
    config["firmware_repo"] = RELEASE_REPO
    config["artifact_source"] = "release"
    config["publish_path"] = None
    config.pop("firmware_url", None)
    if mqtt_host is not None:
        config["mqtt_host"] = mqtt_host
    if changed:
        temporary = path.with_name(path.name + ".tmp")
        temporary.write_text(json.dumps(config, ensure_ascii=True, indent=2) + "\n", encoding="utf-8")
        os.replace(temporary, path)
        print("OTA: odswiezono lokalny secrets.ota.json (bez zmiany hasel)")
    return config


def github_credential() -> str:
    result = subprocess.run(
        ["git", "credential", "fill"], input="protocol=https\nhost=github.com\n\n",
        cwd=ROOT, text=True, capture_output=True, check=False,
    )
    if result.returncode:
        raise OtaUploadError("Brak logowania GitHub w Git Credential Manager")
    for line in result.stdout.splitlines():
        if line.startswith("password=") and line[9:]:
            return line[9:]
    raise OtaUploadError("Git Credential Manager nie zwrocil tokenu GitHub")


def github_request(
    method: str, url: str, token: str, payload: Optional[bytes] = None,
    content_type: str = "application/vnd.github+json",
) -> Dict[str, Any]:
    request = urllib.request.Request(
        url, data=payload, method=method,
        headers={
            "Authorization": "Bearer " + token,
            "Accept": "application/vnd.github+json",
            "Content-Type": content_type,
            "User-Agent": "sterownik-led-publish-ota",
            "X-GitHub-Api-Version": "2022-11-28",
        },
    )
    try:
        with urllib.request.urlopen(request, timeout=60) as response:
            return json.load(response)
    except urllib.error.HTTPError as error:
        if error.code == 404 and method == "GET":
            return {}
        raise OtaUploadError("GitHub API %s: HTTP %u" % (method, error.code)) from error
    except (OSError, ValueError) as error:
        raise OtaUploadError("GitHub API %s: blad odpowiedzi" % method) from error


def verify_published_release(tag: str, commit: str, config: Dict[str, Any]) -> str:
    url = firmware_url_for_version(config, tag[1:])
    manifest_url = url.rsplit("/", 1)[0] + "/release.json"
    try:
        with urllib.request.urlopen(manifest_url, timeout=30) as response:
            manifest = json.load(response)
    except (OSError, ValueError) as error:
        raise OtaUploadError("Nie mozna sprawdzic publicznego release.json") from error
    if not isinstance(manifest, dict) or manifest.get("source_commit") != commit:
        raise OtaUploadError("Publiczny Release wskazuje inny commit zrodel")
    return verify_release_artifact(url, tag[1:])


def publish_release(tag: str, commit: str, assets_dir: Path, config: Dict[str, Any]) -> str:
    token = github_credential()
    release_url = API_ROOT + "/releases/tags/" + urllib.parse.quote(tag)
    release = github_request("GET", release_url, token)
    if release and not release.get("draft"):
        print("OTA: publiczny Release %s juz istnieje; sprawdzam go" % tag)
        local_manifest = json.loads((assets_dir / 'release.json').read_text(encoding='utf-8'))
        url = verify_published_release(tag, commit, config)
        verify_remote_artifact(url, local_manifest['sha256'], local_manifest['size'])
        return url
    if release:
        if release.get("assets"):
            raise OtaUploadError("Istnieje niekompletny draft Release; wymaga sprawdzenia na GitHub")
    else:
        body = {"tag_name": tag, "name": tag, "draft": True, "prerelease": False,
                "body": (assets_dir / "RELEASE_NOTES.md").read_text(encoding="utf-8")}
        release = github_request(
            "POST", API_ROOT + "/releases", token, json.dumps(body).encode("utf-8")
        )
        print("OTA: utworzono szkic wydania %s" % tag)
    upload_base = release["upload_url"].split("{", 1)[0]
    for name in ("firmware.bin", "release.json", "SHA256SUMS"):
        source = assets_dir / name
        upload_url = upload_base + "?name=" + urllib.parse.quote(name)
        github_request(
            "POST", upload_url, token, source.read_bytes(), "application/octet-stream"
        )
        print("OTA: wyslano %s (%u B)" % (name, source.stat().st_size))
    github_request(
        "PATCH", API_ROOT + "/releases/%u" % release["id"], token,
        json.dumps({"draft": False, "make_latest": "true"}).encode("utf-8"),
    )
    print("OTA: wydanie %s jest publiczne" % tag)
    for attempt in range(5):
        try:
            return verify_published_release(tag, commit, config)
        except OtaUploadError:
            if attempt == 4:
                raise
            time.sleep(3)
    raise OtaUploadError("Publiczny Release nie jest dostepny")


def push_tag(tag: str, commit: str) -> None:
    remote = git("ls-remote", "origin", "refs/tags/%s^{}" % tag).split()
    if not remote:
        remote = git("ls-remote", "origin", "refs/tags/%s" % tag).split()
    if remote and remote[0] == commit:
        return
    local = subprocess.run(
        ["git", "rev-parse", "-q", "--verify", "refs/tags/%s^{commit}" % tag],
        cwd=ROOT, text=True, capture_output=True, check=False,
    )
    if local.returncode:
        git("tag", "-a", tag, "-m", "Firmware " + tag[1:])
    git("push", "origin", tag)
    print("OTA: tag %s wypchniety do zrodel" % tag)


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
        if not isinstance(release, dict) or release.get('draft') or release.get('prerelease'):
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
    print('TERAZ: Commit i Push, potem Upload profilu OTA. Build nie publikuje.')


def current_version() -> str:
    cmake = ROOT / 'CMakeLists.txt'
    matches = VERSION_PATTERN.findall(cmake.read_text(encoding='utf-8'))
    if len(matches) != 1:
        raise OtaUploadError('Nie znaleziono jednej wersji PROJECT_VER w CMakeLists.txt')
    return validate_source_version('v' + matches[0], cmake)



def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mqtt-host", help="Nowy adres brokera; zapisz lokalnie")
    parser.add_argument("--publish-only", action="store_true", help="Bez komendy OTA do ESP32")
    parser.add_argument('--image', type=Path, help='Obraz z Upload PlatformIO; nie buduj ponownie')
    parser.add_argument('--prepare-version', action='store_true', help='Ustaw nastepna wersje przed Commit/Push')
    parser.add_argument('--version', help='Wersja dla --prepare-version')
    parser.add_argument('--yes', action='store_true', help='Przyjmij proponowana wersje')
    parser.add_argument('--install-only', action='store_true', help='Zlec instalacje istniejacego wydania')
    args = parser.parse_args()
    if args.prepare_version:
        prepare_version(args.version, args.yes)
        return 0
    if args.version or args.yes:
        parser.error('--version i --yes wymagaja --prepare-version')

    versions = VERSION_PATTERN.findall((ROOT / "CMakeLists.txt").read_text(encoding="utf-8"))
    if len(versions) != 1:
        raise OtaUploadError("Nie znaleziono jednej wersji PROJECT_VER")
    version = validate_source_version("v" + versions[0], ROOT / "CMakeLists.txt")
    tag = "v" + version
    config = {'firmware_repo': RELEASE_REPO, 'artifact_source': 'release', 'publish_path': None}
    if args.install_only:
        config = load_ota_config(ROOT / 'secrets.ota.json', args.mqtt_host)
        url = verify_release_artifact(firmware_url_for_version(config, version), version)
        run_ota(config, url, version)
        return 0

    commit = check_source(tag)
    image_path = ROOT / '.pio' / 'build' / OTA_PROFILE / 'firmware.bin'
    if args.image is None:
        pio = shutil.which('pio') or str(Path(sys.executable).with_name('platformio.exe'))
        print('OTA: buduje profil %s dla %s' % (OTA_PROFILE, tag))
        subprocess.run([pio, 'run', '-e', OTA_PROFILE], cwd=ROOT, check=True)
    elif args.image.resolve() != image_path.resolve():
        raise OtaUploadError('Upload wymaga obrazu profilu ' + OTA_PROFILE)
    # Build must not have changed tracked sources/configuration.
    if check_source(tag) != commit:
        raise OtaUploadError('Zrodla zmienily sie podczas budowania')
    image = image_path.read_bytes()
    manifest = make_manifest(tag, image, commit)
    assets_dir = ROOT / ".pio" / "release"
    write_release_assets(image_path, assets_dir, manifest)
    ota_url = publish_release(tag, commit, assets_dir, config)
    push_tag(tag, commit)
    if args.publish_only:
        print("OTA: wydanie gotowe; pominieto wyslanie komendy do ESP32")
    else:
        config_path = ROOT / 'secrets.ota.json'
        if not config_path.is_file():
            print('OTA: OPUBLIKOWANO. Brak secrets.ota.json; instalacja odroczona do auto-OTA sterownika.')
        else:
            config = load_ota_config(config_path, args.mqtt_host)
            run_ota(config, ota_url, version, allow_offline=True)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OtaUploadError, OSError, subprocess.CalledProcessError) as error:
        print("ERROR OTA: %s" % error, file=sys.stderr)
        raise SystemExit(1)
