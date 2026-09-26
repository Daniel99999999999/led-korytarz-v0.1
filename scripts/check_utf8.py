"""Strict UTF-8/BOM check, portable replacement for iconv in check.sh."""
from pathlib import Path

root = Path(__file__).resolve().parent.parent
extensions = {".c", ".h", ".cpp", ".hpp", ".csv", ".md", ".ini", ".sh", ".py", ".json", ".yml", ".yaml"}
count = 0
for path in root.rglob("*"):
    relative = path.relative_to(root)
    if any(part in {".pio", ".git", "managed_components"} for part in relative.parts):
        continue
    if not path.is_file() or not (path.suffix in extensions or path.name in {
            "CMakeLists.txt", ".clang-format", ".editorconfig", ".gitignore"}
            or path.name.startswith(("sdkconfig", "Kconfig"))):
        continue
    data = path.read_bytes()
    if data.startswith(b"\xef\xbb\xbf"):
        raise SystemExit(f"UTF-8 BOM: {relative}")
    try:
        data.decode("utf-8", errors="strict")
    except UnicodeDecodeError:
        raise SystemExit(f"Invalid UTF-8: {relative}") from None
    count += 1
print(f"UTF-8 without BOM: {count} files PASS")
