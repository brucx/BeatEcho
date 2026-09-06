#!/usr/bin/env python3
"""Beat Echo build, bundle and guarded USB flashing. Python 3.10+; no shell=True."""
from __future__ import annotations
import argparse
from datetime import datetime, timezone
import hashlib
from importlib import metadata
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
FLASH_SIZE = 16 * 1024 * 1024
BOARD = "easyinput-v2-current"
IDF_VERSION = "5.5.5"
ESPTOOL_VERSION = "4.11.0"
# Deliberately fixed to this project's partition layout, not a generic flasher.
LAYOUT = {0x0: "bootloader.bin", 0x8000: "partition-table.bin", 0x10000: "beat_echo.bin"}

class UserError(RuntimeError):
    pass

def digest(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()

def run(command: list[str], *, capture: bool = False, cwd: Path | None = None) -> str:
    print("+ " + subprocess.list2cmdline(command), flush=True)
    completed = subprocess.run(command, cwd=cwd, text=True, encoding="utf-8",
                               errors="replace", stdout=subprocess.PIPE if capture else None,
                               stderr=subprocess.STDOUT if capture else None, check=False)
    output = completed.stdout or ""
    if output:
        print(output, end="" if output.endswith("\n") else "\n", flush=True)
    if completed.returncode:
        raise UserError(f"Command failed ({completed.returncode}); stopped, no automatic retry.")
    return output

def read_json(path: Path) -> dict:
    data = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(data, dict):
        raise UserError(f"Expected JSON object: {path}")
    return data

def private_json(path: Path, data: dict) -> None:
    fd = os.open(str(path), os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2, ensure_ascii=False)
        f.write("\n")

def validate_bundle(directory: Path) -> tuple[dict, list[tuple[int, Path]]]:
    directory = directory.resolve()
    manifest = read_json(directory / "manifest.json")
    if (manifest.get("schema") != 1 or manifest.get("board") != BOARD
            or manifest.get("chip") != "esp32s3" or manifest.get("flash_size") != FLASH_SIZE):
        raise UserError("Wrong bundle schema/board/chip/Flash size. Only EasyInput V2.0 is accepted.")
    files = manifest.get("files")
    if not isinstance(files, list) or len(files) != 3:
        raise UserError("Bundle must contain exactly bootloader, partition table and application.")
    result, seen = [], set()
    for item in files:
        if not isinstance(item, dict):
            raise UserError("Invalid file entry")
        offset = item.get("offset")
        if type(offset) is not int or offset not in LAYOUT or offset in seen:
            raise UserError("Invalid or duplicate Flash offset")
        name = item.get("name")
        if name != LAYOUT[offset]:
            raise UserError("Unexpected filename/path; refusing unsafe bundle")
        path = directory / name
        if path.is_symlink() or not path.is_file() or path.resolve().parent != directory:
            raise UserError(f"Missing/unsafe binary: {name}")
        size = path.stat().st_size
        if type(item.get("size")) is not int or size != item["size"] or size <= 0:
            raise UserError(f"Size mismatch: {name}")
        if not isinstance(item.get("sha256"), str) or digest(path) != item["sha256"]:
            raise UserError(f"SHA-256 mismatch: {name}")
        # Respect erase-sector boundaries and the fixed 2 MiB application partition.
        limit = {0: 0x8000, 0x8000: 0x9000, 0x10000: 0x210000}[offset]
        if offset + ((size + 4095) // 4096) * 4096 > limit:
            raise UserError(f"Binary exceeds its region: {name}")
        header = path.open("rb")
        with header:
            first = header.read(24)
        if offset != 0x8000:
            if len(first) < 24 or first[0] != 0xE9 or struct.unpack_from("<H", first, 12)[0] != 9:
                raise UserError(f"Not an ESP32-S3 image: {name}")
        elif len(first) < 2 or first[:2] != b"\xaa\x50":
            raise UserError("Invalid ESP-IDF partition table magic")
        seen.add(offset)
        result.append((offset, path))
    return manifest, sorted(result)

def idf() -> list[str]:
    path = os.environ.get("IDF_PATH")
    script = Path(path) / "tools" / "idf.py" if path else None
    if not script or not script.is_file():
        raise UserError("Activate ESP-IDF 5.5.5 first (export.sh / ESP-IDF terminal). See docs/BUILD.md.")
    command = [sys.executable, str(script)]
    version = run(command + ["--version"], capture=True)
    if not re.search(r"\bv5\.5\.5(?:\b|[-+])", version):
        raise UserError(f"This project requires ESP-IDF {IDF_VERSION}; do not silently change SDK versions.")
    return command

def package(build_dir: Path, out: Path) -> None:
    args = read_json(build_dir / "flasher_args.json")
    project = read_json(build_dir / "project_description.json")
    if project.get("target") != "esp32s3" or project.get("project_name") != "beat_echo":
        raise UserError("Build output is not the Beat Echo / ESP32-S3 project")
    settings = args.get("flash_settings", {})
    if settings.get("flash_size") != "16MB" or settings.get("flash_mode") != "dio":
        raise UserError("Expected the project's 16MB / DIO build settings")
    if out.exists() or Path(str(out) + ".zip").exists():
        raise UserError(f"Output already exists: {out}. Choose --out or explicitly remove the old generated bundle.")
    commit = os.environ.get("GITHUB_SHA", "local-uncommitted")
    if commit == "local-uncommitted" and (ROOT / ".git").exists():
        commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
        if subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT, text=True).strip():
            commit += "+dirty"
    out.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="be-package-", dir=out.parent) as temporary:
        stage = Path(temporary) / "bundle"
        stage.mkdir()
        entries = []
        flash_files = args.get("flash_files", {})
        if not isinstance(flash_files, dict) or len(flash_files) != 3:
            raise UserError("Unexpected build Flash file map")
        for address, filename in flash_files.items():
            offset = int(address, 0)
            if offset not in LAYOUT:
                raise UserError(f"Unexpected offset {address}; update layout only after review")
            source = (build_dir / filename).resolve()
            if not source.is_relative_to(build_dir.resolve()) or not source.is_file():
                raise UserError("Build file points outside build directory")
            target = stage / LAYOUT[offset]
            shutil.copyfile(source, target)
            entries.append({"name": target.name, "offset": offset, "size": target.stat().st_size,
                            "sha256": digest(target)})
        manifest = {"schema": 1, "board": BOARD, "chip": "esp32s3", "flash_size": FLASH_SIZE,
                    "idf_version": project.get("git_revision", project.get("idf_ver", "unknown")), "source_commit": commit,
                    "created_utc": datetime.now(timezone.utc).isoformat(),
                    "hardware_verified": False, "files": sorted(entries, key=lambda x: x["offset"])}
        (stage / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        (stage / "scripts").mkdir()
        for name in ("be.py", "flash.sh", "flash.ps1"):
            shutil.copyfile(ROOT / "scripts" / name, stage / "scripts" / name)
        shutil.copyfile(ROOT / "requirements-flash.txt", stage / "requirements-flash.txt")
        shutil.copyfile(ROOT / "docs" / "FLASHING.md", stage / "FLASHING.md")
        (stage / "README.md").write_text(
            "# Beat Echo USB firmware bundle\n\nRead FLASHING.md before writing hardware.\n"
            "Python 3.10+ is required; ESP-IDF is NOT required for this prebuilt bundle.\n\n"
            "```sh\npython -m pip install -r requirements-flash.txt\n"
            "python scripts/be.py ports\npython scripts/be.py verify-package\n"
            "python scripts/be.py flash --port YOUR_PORT\n```\n\n"
            "Use python3 on macOS/Linux or py -3 on Windows. Activate a virtual environment first.\n"
            "Default flash backs up all 16 MiB, checks identity and verifies written bytes.\n"
            "No hardware testing is implied by a successful target build.\n", encoding="utf-8")
        validate_bundle(stage)
        shutil.move(str(stage), str(out))
    archive = Path(str(out) + ".zip")
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as z:
        for file in sorted(out.rglob("*")):
            if file.is_file():
                z.write(file, file.relative_to(out))
    archive.with_suffix(".zip.sha256").write_text(f"{digest(archive)}  {archive.name}\n", encoding="ascii")
    print(f"Created {archive}; compiled images only, NOT hardware-qualified.")

def require_flash_tools() -> None:
    try:
        version = metadata.version("esptool")
        metadata.version("pyserial")
    except metadata.PackageNotFoundError as e:
        raise UserError("Install requirements-flash.txt in a virtual environment first.") from e
    if version != ESPTOOL_VERSION:
        raise UserError(f"Expected esptool {ESPTOOL_VERSION}, found {version}. Use the flash virtual environment.")

def esp_command(port: str, baud: int, *operation: str, rom: bool = False) -> list[str]:
    command = [sys.executable, "-m", "esptool", "--chip", "esp32s3", "--port", port,
               "--baud", str(baud), "--before", "no_reset", "--after", "no_reset"]
    if rom:
        command += ["--no-stub"]
    return command + list(operation)

def identify(port: str, baud: int) -> str:
    security = run(esp_command(port, baud, "get_security_info", rom=True), capture=True)
    for label in ("Secure Boot", "Flash Encryption"):
        if not re.search(rf"^{label}: Disabled\s*$", security, flags=re.M):
            raise UserError(f"{label} is enabled or cannot be verified; refusing this unencrypted development firmware.")
    output = run(esp_command(port, baud, "flash_id"), capture=True)
    if not re.search(r"Detected flash size:\s*16MB\b", output):
        raise UserError("Expected exactly 16MB Flash; refused. Never force a different target.")
    mac = re.findall(r"^MAC:\s*([0-9a-fA-F:]{17})\s*$", output, flags=re.M)
    if not mac:
        raise UserError("Could not confirm device MAC; refused")
    return mac[-1].lower()

def backup(port: str, baud: int, mac: str, directory: Path) -> Path:
    directory.mkdir(parents=True, exist_ok=True, mode=0o700)
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    dest = directory / f"easyinput-{stamp}-{mac.replace(':', '')}.bin"
    temporary = dest.with_suffix(".partial")
    # Reserve the path privately before esptool opens it; never overwrite a backup.
    fd = os.open(str(temporary), os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
    os.close(fd)
    try:
        run(esp_command(port, baud, "read_flash", "0x0", hex(FLASH_SIZE), str(temporary)))
        if temporary.stat().st_size != FLASH_SIZE:
            raise UserError("Backup is incomplete; will not flash")
        os.replace(temporary, dest)
        private_json(dest.with_suffix(".json"), {"schema": 1, "board": BOARD, "chip": "esp32s3",
                     "flash_size": FLASH_SIZE, "mac": mac, "sha256": digest(dest),
                     "created_utc": datetime.now(timezone.utc).isoformat()})
    except BaseException:
        temporary.unlink(missing_ok=True)
        raise
    print(f"Private backup: {dest}\nKeep the .bin AND .json together. Never upload a device backup.")
    return dest

def confirm(port: str, action: str, accepted: bool) -> None:
    print(f"\n{action} | port={port} | EasyInput V2.0 / AI Keyboard V2.1 only.")
    print("Keep board powered ON. Tap and release BOOT ONCE. Close other serial monitors.")
    print("Writing replaces the current firmware. Chip checks cannot identify PCB wiring.")
    if accepted:
        return
    if not sys.stdin.isatty():
        raise UserError("Interactive consent required; --yes is explicit confirmation for scripted use.")
    if input("Confirm the physical board and port; type FLASH to continue: ").strip() != "FLASH":
        raise UserError("Cancelled; no device operation performed")

def default_bundle() -> Path:
    return ROOT if (ROOT / "manifest.json").exists() else ROOT / "dist" / "beat-echo-esp32s3"

def flash(args: argparse.Namespace) -> None:
    manifest, parts = validate_bundle(args.bundle)
    print(f"Bundle source: {manifest.get('source_commit', 'unknown')}; SHA-256 checks passed.")
    command_parts = [v for offset, path in parts for v in (hex(offset), str(path))]
    write = esp_command(args.port, args.baud, "write_flash", "--flash_mode", "keep",
                        "--flash_freq", "keep", "--flash_size", "keep", *command_parts)
    verify = esp_command(args.port, args.baud, "verify_flash", *command_parts)
    if args.dry_run:
        print("DRY RUN: no serial port is opened; identity, full backup, write and verify would run.")
        print(subprocess.list2cmdline(write)); print(subprocess.list2cmdline(verify)); return
    require_flash_tools()
    confirm(args.port, "FLASH (full backup enabled)" if not args.skip_backup else "FLASH WITHOUT BACKUP", args.yes)
    mac = identify(args.port, args.baud)
    if not args.skip_backup:
        backup(args.port, args.baud, mac, args.backup_dir)
    run(write)
    run(verify)
    print("Flash and read-back verification succeeded. Power OFF then ON; re-list ports before monitoring.")
    print("This verifies stored bytes, not audio, electrical safety or gameplay on the board.")

def restore(args: argparse.Namespace) -> None:
    path = args.file.resolve()
    info = read_json(path.with_suffix(".json"))
    if (info.get("schema") != 1 or info.get("board") != BOARD or info.get("chip") != "esp32s3"
            or info.get("flash_size") != FLASH_SIZE or path.stat().st_size != FLASH_SIZE
            or digest(path) != info.get("sha256") or not re.fullmatch(r"(?:[0-9a-f]{2}:){5}[0-9a-f]{2}", info.get("mac", ""))):
        raise UserError("Backup or sidecar is invalid; restore refused")
    require_flash_tools()
    confirm(args.port, "RESTORE: overwrites the full 16 MiB", args.yes)
    if identify(args.port, args.baud) != info["mac"]:
        raise UserError("Backup belongs to a different chip; restore refused")
    run(esp_command(args.port, args.baud, "write_flash", "--flash_mode", "keep", "--flash_freq", "keep",
                    "--flash_size", "keep", "0x0", str(path)))
    run(esp_command(args.port, args.baud, "verify_flash", "0x0", str(path)))
    print("Restore verified. Power OFF then ON. Flash backups do not restore eFuses or external devices.")

def ports() -> None:
    try:
        from serial.tools import list_ports
    except ImportError as e:
        raise UserError("Install requirements-flash.txt to enumerate USB ports.") from e
    found = list(list_ports.comports())
    for p in found:
        vid_pid = f"{p.vid:04x}:{p.pid:04x}" if p.vid is not None and p.pid is not None else "unknown"
        print(f"{p.device}\t{vid_pid}\t{p.description}")
    if not found:
        print("No ports found. Use a DATA USB cable; power ON and tap/release BOOT once.")
    print("No port is selected automatically. A USB identity is not proof of the PCB model.")

def monitor(port: str) -> None:
    try:
        import serial
    except ImportError as e:
        raise UserError("Install requirements-flash.txt first.") from e
    connection = serial.Serial(port=None, baudrate=115200, timeout=0.2)
    connection.dtr = False; connection.rts = False; connection.port = port
    connection.open()
    print("Read-only log monitor; Ctrl+C to exit. No intentional reset is issued.")
    import codecs
    decoder = codecs.getincrementaldecoder("utf-8")("replace")
    try:
        while True:
            data = connection.read(4096)
            if data:
                print(decoder.decode(data), end="", flush=True)
    except KeyboardInterrupt:
        print("\nMonitor stopped.")
    finally:
        connection.close()

def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="action", required=True)
    sub.add_parser("doctor", help="Show local tools; never opens hardware")
    sub.add_parser("ports", help="List serial ports without opening them")
    sub.add_parser("configure", help="ESP-IDF menuconfig; never flashes")
    sub.add_parser("build", help="Build using activated ESP-IDF 5.5.5; never flashes")
    pack = sub.add_parser("package", help="Package successful ESP-IDF output")
    pack.add_argument("--build-dir", type=Path, default=ROOT / "firmware" / "build")
    pack.add_argument("--out", type=Path, default=ROOT / "dist" / "beat-echo-esp32s3")
    verify = sub.add_parser("verify-package", help="Offline image/hash validation")
    verify.add_argument("--bundle", type=Path, default=default_bundle())
    for name in ("flash", "backup", "restore"):
        p = sub.add_parser(name)
        p.add_argument("--port", required=True, help="Explicit physical device port; no autodetection")
        p.add_argument("--baud", type=int, choices=(115200, 230400, 460800, 921600), default=460800)
        p.add_argument("--yes", action="store_true", help="Explicitly confirm board, port and operation")
        if name != "restore":
            p.add_argument("--backup-dir", type=Path, default=ROOT / "backups")
        if name == "flash":
            p.add_argument("--bundle", type=Path, default=default_bundle())
            p.add_argument("--skip-backup", action="store_true", help="Explicitly waive full Flash backup")
            p.add_argument("--dry-run", action="store_true", help="Validate bundle, print plan, never open port")
        if name == "restore":
            p.add_argument("--file", type=Path, required=True, help="Backup .bin with matching .json sidecar")
    mon = sub.add_parser("monitor")
    mon.add_argument("--port", required=True)
    args = parser.parse_args(argv)
    try:
        if args.action == "doctor":
            print(f"Python {sys.version.split()[0]} | {sys.platform}\nProject: {ROOT}")
            print(f"IDF_PATH: {os.environ.get('IDF_PATH', '(not activated)')}")
            for tool in ("esptool", "pyserial"):
                try:
                    print(f"{tool}: {metadata.version(tool)}")
                except metadata.PackageNotFoundError:
                    print(f"{tool}: not installed")
            print("Build needs activated ESP-IDF 5.5.5; prebuilt flashing only needs requirements-flash.txt.")
        elif args.action == "ports": ports()
        elif args.action in ("configure", "build"):
            run(idf() + ["-C", str(ROOT / "firmware"), "menuconfig" if args.action == "configure" else "build"])
        elif args.action == "package": package(args.build_dir.resolve(), args.out.resolve())
        elif args.action == "verify-package":
            m, _ = validate_bundle(args.bundle); print(f"Bundle validated: {m.get('source_commit', 'unknown')}")
        elif args.action == "flash": flash(args)
        elif args.action == "backup":
            require_flash_tools(); confirm(args.port, "READ-ONLY FULL FLASH BACKUP", args.yes)
            backup(args.port, args.baud, identify(args.port, args.baud), args.backup_dir)
        elif args.action == "restore": restore(args)
        elif args.action == "monitor": monitor(args.port)
    except (UserError, OSError, ValueError, KeyError, TypeError, subprocess.SubprocessError) as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print("\nInterrupted. Do not assume a partial write succeeded; see FLASHING.md.", file=sys.stderr)
        return 130
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
