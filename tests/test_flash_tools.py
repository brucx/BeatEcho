"""Offline safety tests. Synthetic images are test fixtures, never real firmware."""
import argparse
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location("be", Path(__file__).resolve().parents[1] / "scripts/be.py")
be = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(be)
MAC = "01:23:45:67:89:ab"
SECURITY = "Secure Boot: Disabled\nFlash Encryption: Disabled\n"
FLASH = f"Chip is ESP32-S3\nMAC: {MAC}\nDetected flash size: 16MB\n"

class FlashToolsTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.bundle = self.root / "bundle"
        self.bundle.mkdir()
        entries = []
        for offset, name in be.LAYOUT.items():
            data = bytearray(256)
            if offset == 0x8000:
                data[:2] = b"\xaa\x50"
            else:
                data[0] = 0xE9
                struct.pack_into("<H", data, 12, 9)
            path = self.bundle / name
            path.write_bytes(data)
            entries.append({"name": name, "offset": offset, "size": len(data), "sha256": be.digest(path)})
        self.manifest = {"schema": 1, "board": be.BOARD, "chip": "esp32s3",
                         "flash_size": be.FLASH_SIZE, "files": entries}
        self.save()
        self.args = argparse.Namespace(bundle=self.bundle, port="COM5", baud=460800,
                                       dry_run=False, yes=True, skip_backup=False,
                                       backup_dir=self.root / "backups")
        self.output = contextlib.redirect_stdout(io.StringIO())
        self.output.__enter__()
        self.addCleanup(self.output.__exit__, None, None, None)

    def save(self):
        (self.bundle / "manifest.json").write_text(json.dumps(self.manifest), encoding="utf-8")

    def reject(self):
        self.save()
        with self.assertRaises(be.UserError): be.validate_bundle(self.bundle)

    def replace_image(self, name, data):
        p = self.bundle / name
        p.write_bytes(data)
        for entry in self.manifest["files"]:
            if entry["name"] == name:
                entry.update(size=len(data), sha256=be.digest(p))
        self.save()

    def test_valid_bundle(self):
        _, parts = be.validate_bundle(self.bundle)
        self.assertEqual([a for a, _ in parts], [0, 0x8000, 0x10000])

    def test_wrong_board(self):
        self.manifest["board"] = "generic-s3"; self.reject()

    def test_wrong_chip(self):
        self.manifest["chip"] = "esp32"; self.reject()

    def test_wrong_flash_capacity(self):
        self.manifest["flash_size"] = 4*1024*1024; self.reject()

    def test_unknown_schema(self):
        self.manifest["schema"] = 2; self.reject()

    def test_hash_mismatch(self):
        (self.bundle / "beat_echo.bin").write_bytes(b"modified"); self.reject()

    def test_missing_file(self):
        (self.bundle / "beat_echo.bin").unlink(); self.reject()

    def test_missing_segment(self):
        self.manifest["files"].pop(); self.reject()

    def test_duplicate_offset(self):
        self.manifest["files"][1]["offset"] = 0; self.reject()

    def test_offset_must_be_integer(self):
        self.manifest["files"][0]["offset"] = "0"; self.reject()

    def test_boolean_is_not_offset(self):
        self.manifest["files"][0]["offset"] = False; self.reject()

    def test_unexpected_offset(self):
        self.manifest["files"][0]["offset"] = 0x1000; self.reject()

    def test_path_traversal(self):
        self.manifest["files"][0]["name"] = "../bootloader.bin"; self.reject()

    def test_absolute_path(self):
        self.manifest["files"][0]["name"] = str((self.root / "bootloader.bin").resolve()); self.reject()

    def test_symlink_not_allowed(self):
        p = self.bundle / "bootloader.bin"
        other = self.root / "real.bin"; other.write_bytes(p.read_bytes()); p.unlink()
        try: p.symlink_to(other)
        except OSError: self.skipTest("OS does not allow symlinks in this test account")
        self.reject()

    def test_wrong_image_magic(self):
        self.replace_image("bootloader.bin", b"x"*256); self.reject()

    def test_wrong_image_chip(self):
        data = bytearray((self.bundle / "beat_echo.bin").read_bytes())
        struct.pack_into("<H", data, 12, 0)
        self.replace_image("beat_echo.bin", data); self.reject()

    def test_wrong_partition_magic(self):
        self.replace_image("partition-table.bin", b"x"*256); self.reject()

    def test_bootloader_cannot_overlap_partitions(self):
        data = (self.bundle / "bootloader.bin").read_bytes() + b"x"*0x8000
        self.replace_image("bootloader.bin", data); self.reject()

    def test_application_cannot_overflow_partition(self):
        data = (self.bundle / "beat_echo.bin").read_bytes() + b"x"*0x200000
        self.replace_image("beat_echo.bin", data); self.reject()

    def test_dry_run_never_opens_serial(self):
        self.args.dry_run = True
        with patch.object(be, "run") as run, patch.object(be, "require_flash_tools") as deps:
            be.flash(self.args)
            run.assert_not_called(); deps.assert_not_called()

    def test_port_required(self):
        with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
            be.main(["flash"])

    def test_noninteractive_requires_explicit_yes(self):
        with patch.object(be.sys.stdin, "isatty", return_value=False), self.assertRaises(be.UserError):
            be.confirm("COM5", "FLASH", False)

    def test_rejected_confirmation_stops_before_hardware(self):
        self.args.yes = False
        with patch.object(be, "require_flash_tools"), patch.object(be.sys.stdin, "isatty", return_value=True), \
                patch("builtins.input", return_value="no"), patch.object(be, "identify") as identify:
            with self.assertRaises(be.UserError): be.flash(self.args)
            identify.assert_not_called()

    def test_command_never_forces_or_resets(self):
        cmd = be.esp_command("/some path/port", 460800, "write_flash", "0x0", "file with spaces.bin")
        self.assertIn("/some path/port", cmd); self.assertIn("file with spaces.bin", cmd)
        self.assertNotIn("--force", cmd); self.assertNotIn("erase_flash", cmd)
        self.assertEqual(cmd[cmd.index("--before")+1], "no_reset")
        self.assertEqual(cmd[cmd.index("--after")+1], "no_reset")
        self.assertEqual(cmd[cmd.index("--chip")+1], "esp32s3")

    def test_identify_unencrypted_s3(self):
        with patch.object(be, "run", side_effect=[SECURITY, FLASH]):
            self.assertEqual(be.identify("COM5", 460800), MAC)

    def test_security_enabled_blocks_device(self):
        for label in ("Secure Boot", "Flash Encryption"):
            with self.subTest(label=label), patch.object(be, "run", return_value=SECURITY.replace(f"{label}: Disabled", f"{label}: Enabled")) as run:
                with self.assertRaises(be.UserError): be.identify("COM5", 460800)
                self.assertEqual(run.call_count, 1)

    def test_unknown_security_output_blocks_device(self):
        with patch.object(be, "run", return_value="unknown"), self.assertRaises(be.UserError):
            be.identify("COM5", 460800)

    def test_device_wrong_flash_size_blocks(self):
        with patch.object(be, "run", side_effect=[SECURITY, FLASH.replace("16MB", "4MB")]), self.assertRaises(be.UserError):
            be.identify("COM5", 460800)

    def test_device_missing_mac_blocks(self):
        with patch.object(be, "run", side_effect=[SECURITY, "Detected flash size: 16MB\n"]), self.assertRaises(be.UserError):
            be.identify("COM5", 460800)

    def test_default_flash_order_and_verify(self):
        events = []
        with patch.object(be, "require_flash_tools"), \
             patch.object(be, "identify", side_effect=lambda *_: events.append("identify") or MAC), \
             patch.object(be, "backup", side_effect=lambda *_: events.append("backup")), \
             patch.object(be, "run", side_effect=lambda c, **_: events.append("write" if "write_flash" in c else "verify")):
            be.flash(self.args)
        self.assertEqual(events, ["identify", "backup", "write", "verify"])

    def test_backup_failure_prevents_write(self):
        with patch.object(be, "require_flash_tools"), patch.object(be, "identify", return_value=MAC), \
                patch.object(be, "backup", side_effect=be.UserError("failed")), patch.object(be, "run") as run:
            with self.assertRaises(be.UserError): be.flash(self.args)
            run.assert_not_called()

    def test_write_failure_prevents_success_and_verify(self):
        with patch.object(be, "require_flash_tools"), patch.object(be, "identify", return_value=MAC), \
                patch.object(be, "backup"), patch.object(be, "run", side_effect=be.UserError("interrupted")) as run:
            with self.assertRaises(be.UserError): be.flash(self.args)
            self.assertEqual(run.call_count, 1)

    def test_complete_private_backup(self):
        def fake_read(command): Path(command[-1]).write_bytes(b"\xff"*be.FLASH_SIZE)
        with patch.object(be, "run", side_effect=fake_read):
            p = be.backup("COM5", 460800, MAC, self.args.backup_dir)
        info = be.read_json(p.with_suffix(".json"))
        self.assertEqual(info["mac"], MAC); self.assertEqual(info["sha256"], be.digest(p))
        self.assertEqual(p.stat().st_size, be.FLASH_SIZE)

    def test_incomplete_backup_rejected(self):
        def fake_read(command): Path(command[-1]).write_bytes(b"partial")
        with patch.object(be, "run", side_effect=fake_read), self.assertRaises(be.UserError):
            be.backup("COM5", 460800, MAC, self.args.backup_dir)
        self.assertFalse(list(self.args.backup_dir.glob("*.bin")))
        self.assertFalse(list(self.args.backup_dir.glob("*.partial")))

    def test_restore_refuses_other_chip(self):
        p = self.root / "backup.bin"; p.write_bytes(b"\xff"*be.FLASH_SIZE)
        p.with_suffix(".json").write_text(json.dumps({"schema": 1, "board": be.BOARD, "chip": "esp32s3",
          "flash_size": be.FLASH_SIZE, "mac": MAC, "sha256": be.digest(p)}), encoding="utf-8")
        self.args.file = p
        with patch.object(be, "require_flash_tools"), patch.object(be, "identify", return_value="aa:bb:cc:dd:ee:ff"), \
                patch.object(be, "run") as run:
            with self.assertRaises(be.UserError): be.restore(self.args)
            run.assert_not_called()

    def build_fixture(self):
        build = self.root / "build"; build.mkdir()
        mapping = {}
        for offset, name in be.LAYOUT.items():
            (build / name).write_bytes((self.bundle / name).read_bytes()); mapping[hex(offset)] = name
        (build / "flasher_args.json").write_text(json.dumps({"flash_settings": {"flash_size": "16MB", "flash_mode": "dio"}, "flash_files": mapping}), encoding="utf-8")
        (build / "project_description.json").write_text(json.dumps({"target": "esp32s3", "project_name": "beat_echo", "idf_ver": "v5.5.5"}), encoding="utf-8")
        return build

    def test_package_roundtrip_includes_tools(self):
        out = self.root / "release.v0.1"
        be.package(self.build_fixture(), out)
        be.validate_bundle(out)
        for name in ("scripts/be.py", "scripts/flash.sh", "scripts/flash.ps1", "FLASHING.md", "requirements-flash.txt"):
            self.assertTrue((out / name).is_file(), name)
        self.assertTrue(Path(str(out)+".zip").is_file())
        self.assertFalse(be.read_json(out / "manifest.json")["hardware_verified"])

    def test_package_refuses_overwrite(self):
        build = self.build_fixture(); out = self.root / "release"
        be.package(build, out)
        with self.assertRaises(be.UserError): be.package(build, out)

    def test_package_rejects_external_file(self):
        build = self.build_fixture()
        m = be.read_json(build / "flasher_args.json")
        m["flash_files"]["0x0"] = "../bundle/bootloader.bin"
        (build / "flasher_args.json").write_text(json.dumps(m), encoding="utf-8")
        with self.assertRaises(be.UserError): be.package(build, self.root / "release")

    def test_doctor_does_not_open_hardware(self):
        with patch.object(be, "run") as run:
            self.assertEqual(be.main(["doctor"]), 0); run.assert_not_called()

if __name__ == "__main__": unittest.main()
