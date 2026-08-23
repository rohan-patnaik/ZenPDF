#!/usr/bin/env python3

from __future__ import annotations

import argparse
import collections
import datetime as dt
import fcntl
import hashlib
import json
import os
import re
import secrets
import shutil
import signal
import stat
import subprocess
import sys
import tarfile
import time
from pathlib import Path
from typing import Any


IMPLEMENTATION_SHA = "aa118d8a9544e4982ef8110988f95e45fa9ccf3b"
IMPLEMENTATION_TREE = "649c6283b40f22b672f3f4f436ddab03b9809051"
PARENT_SHA = "f2fac2351658c677afc4519e4b44dcf0fad30592"
PACKAGE_VERSION = "0.1.0.r0.gaa118d8-1"
PACKAGE_SHA256 = "0767a68df9aead7f9ec5bfeb595b8dcc0d7ba4ed93b48c5925d1ebc2ed2dac99"
SOURCE_SHA256 = "c1a5c8699647ad08865fccbe05efe13d8cad72d404bdc2764cb1bb8ebe8f17f8"
EXECUTABLE_SHA256 = "a48f20268555796a7f159508a27cd6a36c2561a936cfb7767c5a4ea072292416"
FIXTURE_SHA256 = "893fd90c2553f6a0b075711da52d0433ba73345b0f4f0df2e3f2ded1f3805122"
ROLLBACK_SHA256 = "d231fc58f62847fbf2603b9b29dcf54334457d83b47a3a0cc2ba88b67b6402e2"
HOST_PACKAGE_VERSION = "0.1.0.r0.g99b7528-1"
HOST_EXECUTABLE_SHA256 = "c9db39c18fb08c820a6469ad0f1a60499b4a1653c0dcec66673f6d8c082e32ee"
MAX_LOG_BYTES = 1024 * 1024
SENSITIVE_BODY = (
    "/redacted/private/L005-PATH-8d1317/document-L005-BASENAME-8d1317.pdf "
    "PASSWORD-L005-8d1317 TEXT-L005-8d1317 line1\nline2\rESC\x1b[31m"
)
FORBIDDEN = (
    b"L005-PATH-8d1317",
    b"document-L005-BASENAME-8d1317.pdf",
    b"PASSWORD-L005-8d1317",
    b"TEXT-L005-8d1317",
    b"L005-PRIVATE-CATEGORY-8d1317",
    b"l005_probe.cpp",
    b"emitSensitiveWarning",
    b"line1",
    b"line2",
    b"\x1b[31m",
    b"qt.qpa",
    b"could not connect to display",
    b"cannot open display",
)
LOG_LINE = re.compile(
    rb"^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{3}Z "
    rb"\[(debug|info|warning|critical|fatal)\] zenpdf: "
    rb"(application-start|message-suppressed)$"
)
FILLER_LINES = (
    b"2026-08-23T00:00:00.000Z [info] zenpdf: application-start\n",
    b"2026-08-23T00:00:00.000Z [warning] zenpdf: message-suppressed\n",
    b"2026-08-23T00:00:00.000Z [critical] zenpdf: message-suppressed\n",
)


def check(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def command(argv: list[str], *, timeout: float = 30.0, check_status: bool = True) -> subprocess.CompletedProcess[bytes]:
    result = subprocess.run(argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=timeout)
    if check_status and result.returncode != 0:
        raise AssertionError(
            f"command failed ({result.returncode}): {argv!r}\n"
            f"stdout={result.stdout[-2048:]!r}\nstderr={result.stderr[-2048:]!r}"
        )
    return result


def write_json(path: Path, value: Any) -> None:
    descriptor = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_TRUNC | os.O_CLOEXEC, 0o600)
    try:
        payload = (json.dumps(value, indent=2, sort_keys=True) + "\n").encode()
        os.write(descriptor, payload)
        os.fsync(descriptor)
    finally:
        os.close(descriptor)


def metadata(path: Path) -> dict[str, Any]:
    try:
        value = path.lstat()
    except FileNotFoundError:
        return {"exists": False}
    kind = (
        "regular" if stat.S_ISREG(value.st_mode) else
        "directory" if stat.S_ISDIR(value.st_mode) else
        "symlink" if stat.S_ISLNK(value.st_mode) else
        "fifo" if stat.S_ISFIFO(value.st_mode) else
        "socket" if stat.S_ISSOCK(value.st_mode) else
        "other"
    )
    result: dict[str, Any] = {
        "exists": True,
        "type": kind,
        "owner_matches_euid": value.st_uid == os.geteuid(),
        "group_matches_egid": value.st_gid == os.getegid(),
        "mode": f"{stat.S_IMODE(value.st_mode):04o}",
        "nlink": value.st_nlink,
        "size": value.st_size,
        "device": value.st_dev,
        "inode": value.st_ino,
    }
    if kind == "regular":
        try:
            result["sha256"] = sha256_file(path)
        except PermissionError:
            result["content_readable"] = False
    elif kind == "symlink":
        result["target_sha256"] = sha256_bytes(os.readlink(path).encode())
    elif kind == "directory":
        try:
            result["entries"] = sorted(child.name for child in path.iterdir())
        except PermissionError:
            result["entries_readable"] = False
    return result


def stable_metadata(value: dict[str, Any]) -> dict[str, Any]:
    return {key: item for key, item in value.items() if key not in {"device", "inode"}}


def exact_filler(size: int) -> tuple[bytes, dict[str, int]]:
    first, second, third = FILLER_LINES
    for first_count in range(0, min(size // len(first), 2048) + 1):
        after_first = size - first_count * len(first)
        for second_count in range(0, min(after_first // len(second), 2048) + 1):
            remainder = after_first - second_count * len(second)
            if remainder >= 0 and remainder % len(third) == 0:
                third_count = remainder // len(third)
                data = first * first_count + second * second_count + third * third_count
                check(len(data) == size, "filler size mismatch")
                return data, {
                    "application-start": first_count,
                    "message-suppressed": second_count + third_count,
                }
    raise AssertionError(f"cannot represent exact valid filler size {size}")


def write_private(path: Path, data: bytes, mode: int = 0o600) -> None:
    descriptor = os.open(
        path,
        os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_CLOEXEC,
        mode,
    )
    try:
        offset = 0
        while offset < len(data):
            offset += os.write(descriptor, data[offset:])
        os.fsync(descriptor)
    finally:
        os.close(descriptor)
    os.chmod(path, mode)


def rewrite_private(path: Path, data: bytes, mode: int = 0o600) -> None:
    descriptor = os.open(
        path,
        os.O_WRONLY | os.O_CREAT | os.O_TRUNC | os.O_CLOEXEC,
        mode,
    )
    try:
        offset = 0
        while offset < len(data):
            offset += os.write(descriptor, data[offset:])
        os.fsync(descriptor)
    finally:
        os.close(descriptor)
    os.chmod(path, mode)


def parse_fixed_events(data: bytes) -> dict[str, Any]:
    lines = data.splitlines()
    invalid = [line.decode("utf-8", "replace") for line in lines if LOG_LINE.fullmatch(line) is None]
    counts: collections.Counter[str] = collections.Counter()
    severity: collections.Counter[str] = collections.Counter()
    for line in lines:
        match = LOG_LINE.fullmatch(line)
        if match is not None:
            severity[match.group(1).decode()] += 1
            counts[match.group(2).decode()] += 1
    forbidden_hits = [token.decode("utf-8", "replace") for token in FORBIDDEN if token.lower() in data.lower()]
    return {
        "bytes": len(data),
        "sha256": sha256_bytes(data),
        "line_count": len(lines),
        "event_counts": dict(sorted(counts.items())),
        "severity_counts": dict(sorted(severity.items())),
        "invalid_lines": invalid,
        "forbidden_hits": forbidden_hits,
    }


def verify_fixed(data: bytes, *, expected_events: dict[str, int] | None = None) -> dict[str, Any]:
    result = parse_fixed_events(data)
    check(not result["invalid_lines"], f"non-fixed diagnostic lines: {result['invalid_lines']!r}")
    check(not result["forbidden_hits"], f"forbidden diagnostic content: {result['forbidden_hits']!r}")
    if expected_events is not None:
        check(result["event_counts"] == expected_events, (
            f"event counts {result['event_counts']!r}, expected {expected_events!r}"
        ))
    return result


def log_snapshot(log_directory: Path, *, verify: bool = True) -> dict[str, Any]:
    directory = metadata(log_directory)
    entries: dict[str, Any] = {}
    if directory.get("type") == "directory":
        for child in sorted(log_directory.iterdir(), key=lambda item: item.name):
            child_metadata = metadata(child)
            if child.is_file() and not child.is_symlink():
                data = child.read_bytes()
                child_metadata["events"] = parse_fixed_events(data)
                if verify and child.name in {"zenpdf.log", "zenpdf.log.1"}:
                    check(child_metadata["type"] == "regular", "log is not regular")
                    check(child_metadata["owner_matches_euid"], "log owner mismatch")
                    check(child_metadata["mode"] == "0600", "log mode mismatch")
                    check(child_metadata["nlink"] == 1, "log is multiply linked")
                    check(child_metadata["size"] <= MAX_LOG_BYTES, "log exceeds 1 MiB")
                    check(not child_metadata["events"]["invalid_lines"], "invalid log grammar")
                    check(not child_metadata["events"]["forbidden_hits"], "forbidden log content")
            entries[child.name] = child_metadata
    if verify and directory.get("type") == "directory":
        check(directory["owner_matches_euid"], "log directory owner mismatch")
        check(directory["mode"] == "0700", "log directory mode mismatch")
        log_names = [name for name in entries if name in {"zenpdf.log", "zenpdf.log.1"}]
        check(len(log_names) <= 2, "more than two log files")
        check(
            sum(entries[name]["size"] for name in log_names) <= 2 * MAX_LOG_BYTES,
            "aggregate log budget exceeded",
        )
    return {"directory": directory, "entries": entries}


def wait_for_path(path: Path, timeout: float) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if path.exists():
            return
        time.sleep(0.01)
    raise AssertionError(f"timed out waiting for {path.name}")


def descendants(pid: int) -> list[int]:
    found: list[int] = []
    pending = [pid]
    while pending:
        parent = pending.pop()
        child_file = Path(f"/proc/{parent}/task/{parent}/children")
        try:
            children = [int(value) for value in child_file.read_text().split()]
        except (FileNotFoundError, PermissionError):
            children = []
        for child in children:
            if child not in found:
                found.append(child)
                pending.append(child)
    return found


def find_app_pid(wrapper_pid: int, timeout: float = 3.0) -> int:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        for pid in descendants(wrapper_pid):
            try:
                if sha256_file(Path(f"/proc/{pid}/exe")) == EXECUTABLE_SHA256:
                    return pid
            except (FileNotFoundError, PermissionError):
                continue
        time.sleep(0.01)
    raise AssertionError("exact installed app process not found")


def deleted_log_descriptors(pid: int) -> list[str]:
    hits: list[str] = []
    for fd in Path(f"/proc/{pid}/fd").iterdir():
        try:
            target = os.readlink(fd)
        except (FileNotFoundError, PermissionError, OSError):
            continue
        if "zenpdf.log" in target and target.endswith(" (deleted)"):
            hits.append(target)
    return sorted(hits)


class Acceptance:
    def __init__(self, args: argparse.Namespace) -> None:
        self.root = args.acceptance_root.resolve()
        self.package_root = args.package_root.resolve()
        self.source_clone = args.source_clone.resolve()
        self.rollback_package = args.rollback_package.resolve()
        self.artifact_package = self.root / "artifacts" / "zenpdf-git-0.1.0.r0.gaa118d8-1-x86_64.pkg.tar.zst"
        self.source_archive = self.root / "artifacts" / f"zenpdf-source-{IMPLEMENTATION_SHA}.tar.gz"
        self.fixture = self.root / "fixtures" / "approved-three-pages.pdf"
        self.probe_source = self.root / "helpers" / "l005_probe.cpp"
        self.probe_map = self.root / "helpers" / "l005_probe.map"
        self.probe = self.root / "helpers" / "l005_probe.so"
        self.fstat_source = self.root / "helpers" / "l005_fstat_fault.c"
        self.fstat_map = self.root / "helpers" / "l005_fstat_fault.map"
        self.fstat_probe = self.root / "helpers" / "l005_fstat_fault.so"
        self.candidate = self.package_root / "usr/bin/zenpdf"
        self.lock_path = Path("/tmp/zenpdf-package-ui-acceptance.lock")
        stamp = dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
        self.run = self.root / "cases" / f"installed-{stamp}"
        self.records = self.root / "records" / f"installed-{stamp}"
        self.run.mkdir(mode=0o700)
        self.records.mkdir(mode=0o700)
        self.results: dict[str, Any] = {
            "schema": 1,
            "started_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
            "implementation": {
                "sha": IMPLEMENTATION_SHA,
                "tree": IMPLEMENTATION_TREE,
                "parent": PARENT_SHA,
            },
            "cases": {},
        }
        self.lock_descriptor = os.open(
            self.lock_path, os.O_RDWR | os.O_CREAT | os.O_CLOEXEC, 0o600
        )
        try:
            fcntl.flock(self.lock_descriptor, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError as error:
            raise AssertionError("package/UI acceptance lock is already held") from error

    def close(self) -> None:
        fcntl.flock(self.lock_descriptor, fcntl.LOCK_UN)
        os.close(self.lock_descriptor)

    def case_root(self, case_id: str) -> Path:
        root = self.run / case_id
        root.mkdir(mode=0o700)
        for name in ("home", "data", "config", "cache", "state", "runtime"):
            (root / name).mkdir(mode=0o700)
        return root

    @staticmethod
    def log_directory(case_root: Path) -> Path:
        return case_root / "data" / "ZenPDF" / "ZenPDF" / "logs"

    def bwrap_command(
        self,
        case_root: Path,
        *,
        mode: str = "default",
        extra_env: dict[str, str] | None = None,
        extra_binds: list[tuple[Path, Path, bool]] | None = None,
        use_fstat_probe: bool = False,
        arguments: list[str] | None = None,
    ) -> list[str]:
        preload = str(self.probe)
        if use_fstat_probe:
            preload += ":" + str(self.fstat_probe)
        environment = {
            "HOME": str(case_root / "home"),
            "PATH": "/usr/bin",
            "LANG": "C.UTF-8",
            "LC_ALL": "C.UTF-8",
            "XDG_DATA_HOME": str(case_root / "data"),
            "XDG_CONFIG_HOME": str(case_root / "config"),
            "XDG_CACHE_HOME": str(case_root / "cache"),
            "XDG_STATE_HOME": str(case_root / "state"),
            "XDG_RUNTIME_DIR": str(case_root / "runtime"),
            "QT_QPA_PLATFORM": "offscreen",
            "QT_QPA_PLATFORMTHEME": "",
            "QT_STYLE_OVERRIDE": "Fusion",
            "LD_PRELOAD": preload,
            "ZENPDF_L005_MODE": mode,
            "ZENPDF_L005_SENSITIVE_BODY": SENSITIVE_BODY,
        }
        if extra_env:
            environment.update(extra_env)
        argv = [
            "bwrap",
            "--die-with-parent",
            "--unshare-user",
            "--uid", "1000",
            "--gid", "1000",
            "--unshare-ipc",
            "--unshare-uts",
            "--ro-bind", "/", "/",
            "--dev-bind", "/dev", "/dev",
            "--proc", "/proc",
            "--bind", str(case_root), str(case_root),
            "--ro-bind", str(self.candidate), "/usr/bin/zenpdf",
            "--chdir", str(case_root),
            "--clearenv",
        ]
        for key, value in environment.items():
            argv.extend(("--setenv", key, value))
        for source, target, writable in extra_binds or []:
            argv.extend(("--bind" if writable else "--ro-bind", str(source), str(target)))
        argv.extend(("--", "/usr/bin/zenpdf"))
        argv.extend(arguments or [])
        return argv

    def start_app(
        self,
        case_root: Path,
        *,
        mode: str = "default",
        extra_env: dict[str, str] | None = None,
        extra_binds: list[tuple[Path, Path, bool]] | None = None,
        use_fstat_probe: bool = False,
        arguments: list[str] | None = None,
        label: str = "run",
    ) -> tuple[subprocess.Popen[bytes], Path, Path, list[str], int]:
        stdout_path = case_root / f"{label}-stdout.bin"
        stderr_path = case_root / f"{label}-stderr.bin"
        stdout_fd = os.open(stdout_path, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_CLOEXEC, 0o600)
        stderr_fd = os.open(stderr_path, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_CLOEXEC, 0o600)
        argv = self.bwrap_command(
            case_root,
            mode=mode,
            extra_env=extra_env,
            extra_binds=extra_binds,
            use_fstat_probe=use_fstat_probe,
            arguments=arguments,
        )
        started = time.monotonic_ns()
        try:
            process = subprocess.Popen(
                argv,
                stdout=stdout_fd,
                stderr=stderr_fd,
                preexec_fn=lambda: os.umask(0o022),
            )
        finally:
            os.close(stdout_fd)
            os.close(stderr_fd)
        return process, stdout_path, stderr_path, argv, started

    def finish_app(
        self,
        process: subprocess.Popen[bytes],
        stdout_path: Path,
        stderr_path: Path,
        started: int,
        *,
        timeout: float = 10.0,
    ) -> dict[str, Any]:
        try:
            return_code = process.wait(timeout=timeout)
        except subprocess.TimeoutExpired as error:
            process.kill()
            process.wait(timeout=2)
            raise AssertionError("installed app exceeded acceptance timeout") from error
        finished = time.monotonic_ns()
        elapsed_ms = (finished - started) / 1_000_000
        stdout = stdout_path.read_bytes()
        stderr = stderr_path.read_bytes()
        os.chmod(stdout_path, 0o600)
        os.chmod(stderr_path, 0o600)
        check(not Path(f"/proc/{process.pid}").exists(), "wrapper process remains")
        return {
            "return_code": return_code,
            "elapsed_ms": round(elapsed_ms, 3),
            "monotonic_start_ns": started,
            "monotonic_end_ns": finished,
            "timeout_s": timeout,
            "stdout_bytes": len(stdout),
            "stdout_sha256": sha256_bytes(stdout),
            "stderr": parse_fixed_events(stderr),
            "stderr_path": stderr_path.name,
        }

    def run_app(self, case_root: Path, **kwargs: Any) -> tuple[dict[str, Any], list[str]]:
        process, stdout, stderr, argv, started = self.start_app(case_root, **kwargs)
        result = self.finish_app(process, stdout, stderr, started)
        return result, argv

    @staticmethod
    def inside_overlay_metadata(
        case_root: Path,
        target: Path,
        extra_binds: list[tuple[Path, Path, bool]],
    ) -> dict[str, Any]:
        argv = [
            "bwrap", "--die-with-parent", "--unshare-user", "--uid", "1000", "--gid", "1000",
            "--ro-bind", "/", "/", "--bind", str(case_root), str(case_root),
            "--clearenv", "--setenv", "LANG", "C", "--setenv", "LC_ALL", "C",
        ]
        for source, destination, writable in extra_binds:
            argv.extend(("--bind" if writable else "--ro-bind", str(source), str(destination)))
        argv.extend(("--", "/usr/bin/stat", "-c", "%F|%u|%g|%a|%h|%s|%d|%i", str(target)))
        values = command(argv).stdout.decode().strip().split("|")
        check(len(values) == 8, "inside overlay stat format mismatch")
        return {
            "type": values[0],
            "uid": int(values[1]),
            "gid": int(values[2]),
            "owner_matches_euid": int(values[1]) == 1000,
            "group_matches_egid": int(values[2]) == 1000,
            "mode": values[3].zfill(4),
            "nlink": int(values[4]),
            "size": int(values[5]),
            "device": int(values[6]),
            "inode": int(values[7]),
        }

    def preflight(self) -> None:
        check(self.root.is_dir(), "acceptance root missing")
        check(command(["stat", "-f", "-c", "%T", str(self.root)]).stdout.strip() == b"btrfs", "acceptance root is not Btrfs")
        check(sha256_file(self.artifact_package) == PACKAGE_SHA256, "package hash mismatch")
        check(sha256_file(self.source_archive) == SOURCE_SHA256, "source archive hash mismatch")
        check(sha256_file(self.candidate) == EXECUTABLE_SHA256, "pacman-root payload hash mismatch")
        check(sha256_file(self.fixture) == FIXTURE_SHA256, "approved fixture hash mismatch")
        check(sha256_file(self.rollback_package) == ROLLBACK_SHA256, "rollback package hash mismatch")

        head = command(["git", "-C", str(self.source_clone), "rev-parse", "HEAD"]).stdout.decode().strip()
        tree = command(["git", "-C", str(self.source_clone), "rev-parse", "HEAD^{tree}"]).stdout.decode().strip()
        status = command(["git", "-C", str(self.source_clone), "status", "--porcelain=v1"]).stdout.decode()
        check(head == IMPLEMENTATION_SHA and tree == IMPLEMENTATION_TREE and status == "", "source clone is not exact and clean")

        package_member = command([
            "bsdtar", "-xOf", str(self.artifact_package), "usr/bin/zenpdf"
        ]).stdout
        check(sha256_bytes(package_member) == EXECUTABLE_SHA256, "package member hash mismatch")
        package_info = command([
            "bsdtar", "-xOf", str(self.artifact_package), ".PKGINFO"
        ]).stdout.decode()
        check(f"pkgver = {PACKAGE_VERSION}" in package_info, "package version mismatch")

        with tarfile.open(self.source_archive, "r:gz") as archive:
            marker_names = [name for name in archive.getnames() if name.endswith("/.zenpdf-source-revision")]
            check(len(marker_names) == 1, "source revision marker missing or ambiguous")
            extracted = archive.extractfile(marker_names[0])
            check(extracted is not None, "source revision marker unreadable")
            source_marker = extracted.read().decode().strip()
        check(source_marker == IMPLEMENTATION_SHA, "source revision marker mismatch")

        pacman_prefix = [
            "unshare", "--user", "--map-root-user",
            "pacman", "--root", str(self.package_root),
            "--dbpath", str(self.package_root / "var/lib/pacman"),
            "--config", str(self.package_root / "etc/pacman.conf"),
        ]
        package_query = command(pacman_prefix + ["-Q", "zenpdf-git"]).stdout.decode().strip()
        package_check = command(pacman_prefix + ["-Qkk", "zenpdf-git"])
        package_check_text = (package_check.stdout + package_check.stderr).decode()
        check(package_query == f"zenpdf-git {PACKAGE_VERSION}", "pacman-root version mismatch")
        check("zenpdf-git: 12 total files, 0 altered files" in package_check_text, "pacman-root Qkk mismatch")

        host_query = command(["pacman", "-Q", "zenpdf-git"]).stdout.decode().strip()
        check(host_query == f"zenpdf-git {HOST_PACKAGE_VERSION}", "host package changed before acceptance")
        check(sha256_file(Path("/usr/bin/zenpdf")) == HOST_EXECUTABLE_SHA256, "host executable changed before acceptance")

        helper_hashes = {
            path.name: sha256_file(path)
            for path in (
                self.probe_source, self.probe_map, self.probe,
                self.fstat_source, self.fstat_map, self.fstat_probe,
            )
        }
        helper_exports = command(["readelf", "--dyn-syms", "--wide", str(self.probe)]).stdout.decode()
        check("_ZN12QApplication4execEv@@Qt_6" in helper_exports, "QApplication::exec interposer export missing")
        fstat_exports = command(["readelf", "--dyn-syms", "--wide", str(self.fstat_probe)]).stdout.decode()
        check("fstat@@GLIBC_2.33" in fstat_exports, "fstat interposer export missing")

        qt_packages = command([
            "pacman", "-Q", "qt6-base", "qt6-webengine", "qpdf", "glibc", "bubblewrap"
        ]).stdout.decode().splitlines()
        ldd = command(["ldd", str(self.candidate)]).stdout.decode().splitlines()
        self.results["preflight"] = {
            "source_clean": True,
            "source_archive_sha256": SOURCE_SHA256,
            "source_revision_marker": source_marker,
            "package_sha256": PACKAGE_SHA256,
            "package_version": PACKAGE_VERSION,
            "package_member_sha256": EXECUTABLE_SHA256,
            "pacman_root_payload_sha256": sha256_file(self.candidate),
            "pacman_query": package_query,
            "pacman_qkk_12_total_0_altered": True,
            "host_package_untouched": {
                "version": HOST_PACKAGE_VERSION,
                "executable_sha256": HOST_EXECUTABLE_SHA256,
            },
            "rollback_package_sha256": ROLLBACK_SHA256,
            "fixture_sha256": FIXTURE_SHA256,
            "filesystem": "btrfs",
            "helper_hashes": helper_hashes,
            "host_runtime_packages": qt_packages,
            "dynamic_loader_resolution": ldd,
            "ci": {
                "run_id": 32651427740,
                "head_sha": IMPLEMENTATION_SHA,
                "conclusion": "success",
                "job_ids": [97223541667, 97223541694, 97223541714, 97223541734, 97223541743],
                "artifact_uploaded": False,
                "claim": "separate exact-tip CI; accepted package is a local exact-source Release package",
            },
        }

    def positive_filesystem_controls(self) -> None:
        root = self.case_root("00-filesystem-controls")
        control = root / "native-fs"
        control.mkdir(mode=0o700)
        original = control / "original"
        renamed = control / "renamed"
        peer = control / "peer"
        fifo = control / "fifo"
        write_private(original, b"L005 positive control\n")
        os.link(original, peer)
        link_count = original.stat().st_nlink
        check(link_count == 2, "hard-link positive control failed")
        os.mkfifo(fifo, 0o600)
        first = os.open(control, os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC)
        second = os.open(control, os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC)
        try:
            fcntl.flock(first, fcntl.LOCK_EX | fcntl.LOCK_NB)
            blocked = False
            try:
                fcntl.flock(second, fcntl.LOCK_EX | fcntl.LOCK_NB)
            except BlockingIOError:
                blocked = True
            check(blocked, "flock positive control did not block")
            fcntl.flock(first, fcntl.LOCK_UN)
            fcntl.flock(second, fcntl.LOCK_EX | fcntl.LOCK_NB)
            fcntl.flock(second, fcntl.LOCK_UN)
        finally:
            os.close(first)
            os.close(second)
        peer.unlink()
        original.rename(renamed)
        check(renamed.read_bytes() == b"L005 positive control\n", "rename/write control failed")
        renamed.unlink()
        fifo.unlink()
        check(not list(control.iterdir()), "positive-control cleanup failed")
        self.results["cases"]["00-filesystem-controls"] = {
            "filesystem": command(["stat", "-f", "-c", "%T", str(control)]).stdout.decode().strip(),
            "create_write": True,
            "hardlink_nlink": link_count,
            "fifo": True,
            "flock_contention_and_recovery": True,
            "rename_unlink": True,
            "clean": True,
        }

    def identity_and_normal(self) -> None:
        root = self.case_root("01-runtime-identity")
        ready = root / "ready"
        trigger = root / "trigger"
        process, stdout, stderr, argv, started = self.start_app(
            root,
            mode="setup",
            extra_env={"ZENPDF_L005_READY": str(ready), "ZENPDF_L005_TRIGGER": str(trigger)},
            label="identity",
        )
        wait_for_path(ready, 3.0)
        app_pid = find_app_pid(process.pid)
        status_lines = Path(f"/proc/{app_pid}/status").read_text().splitlines()
        status_map = {line.split(":", 1)[0]: line.split(":", 1)[1].strip() for line in status_lines if ":" in line}
        environment = Path(f"/proc/{app_pid}/environ").read_bytes().split(b"\0")
        env_map = dict(
            item.decode().split("=", 1) for item in environment if item and b"=" in item
        )
        executable_target = os.readlink(f"/proc/{app_pid}/exe")
        executable_hash = sha256_file(Path(f"/proc/{app_pid}/exe"))
        root_executable_hash = sha256_file(Path(f"/proc/{app_pid}/root/usr/bin/zenpdf"))
        uid_map = Path(f"/proc/{app_pid}/uid_map").read_text().strip()
        gid_map = Path(f"/proc/{app_pid}/gid_map").read_text().strip()
        cwd = os.readlink(f"/proc/{app_pid}/cwd")
        cmdline = [value.decode() for value in Path(f"/proc/{app_pid}/cmdline").read_bytes().split(b"\0") if value]
        mountinfo = Path(f"/proc/{app_pid}/mountinfo").read_text().splitlines()
        case_mounts = [line for line in mountinfo if str(root) in line]
        maps = Path(f"/proc/{app_pid}/maps").read_text().splitlines()
        qt_libraries = sorted({line.split()[-1] for line in maps if "/libQt6" in line})
        host_stat = root.stat()
        process_stat = Path(f"/proc/{app_pid}/root{root}").stat()
        check(executable_target == "/usr/bin/zenpdf", "runtime path is not standard /usr/bin/zenpdf")
        check(executable_hash == EXECUTABLE_SHA256 == root_executable_hash, "runtime executable identity mismatch")
        check(status_map["Uid"].split() == ["1000"] * 4, "runtime UID mismatch")
        check(status_map["Gid"].split() == ["1000"] * 4, "runtime GID mismatch")
        for field in ("CapInh", "CapPrm", "CapEff", "CapAmb"):
            check(int(status_map[field], 16) == 0, f"runtime capability {field} is nonzero")
        check(status_map["Umask"] == "0077", "product umask mismatch")
        check(uid_map.split() == ["1000", "1000", "1"], "runtime uid_map mismatch")
        check(gid_map.split() == ["1000", "1000", "1"], "runtime gid_map mismatch")
        check(cwd == str(root), "runtime cwd mismatch")
        check(env_map["PWD"] == str(root), "runtime PWD mismatch")
        check(cmdline == ["/usr/bin/zenpdf"], "runtime command line mismatch")
        check(host_stat.st_dev == process_stat.st_dev and host_stat.st_ino == process_stat.st_ino, "runtime task root is not host-backed same inode")
        check(case_mounts and any(" - btrfs " in line for line in case_mounts), "runtime case mount is not Btrfs")
        expected_env_keys = {
            "HOME", "PATH", "LANG", "LC_ALL", "XDG_DATA_HOME", "XDG_CONFIG_HOME",
            "XDG_CACHE_HOME", "XDG_STATE_HOME", "XDG_RUNTIME_DIR", "QT_QPA_PLATFORM",
            "QT_QPA_PLATFORMTHEME", "QT_STYLE_OVERRIDE", "LD_PRELOAD", "ZENPDF_L005_MODE",
            "ZENPDF_L005_SENSITIVE_BODY", "PWD",
        }
        expected_env_keys.update({"ZENPDF_L005_READY", "ZENPDF_L005_TRIGGER"})
        check(set(env_map) == expected_env_keys, f"runtime environment not allowlisted: {set(env_map)!r}")
        check("LD_LIBRARY_PATH" not in env_map, "alternate library path present")
        check(env_map["LD_PRELOAD"] == str(self.probe), "unexpected preload payload")
        check(not deleted_log_descriptors(app_pid), "identity process has open-deleted log fd")
        trigger.touch(mode=0o600)
        outcome = self.finish_app(process, stdout, stderr, started)
        check(outcome["return_code"] == 0 and outcome["elapsed_ms"] < 3000, "identity app failed")
        verify_fixed(stderr.read_bytes(), expected_events={"application-start": 1, "message-suppressed": 1})
        logs = log_snapshot(self.log_directory(root))
        combined = b"".join(
            (self.log_directory(root) / name).read_bytes()
            for name in ("zenpdf.log.1", "zenpdf.log")
            if (self.log_directory(root) / name).exists()
        )
        verify_fixed(combined, expected_events={"application-start": 1, "message-suppressed": 1})
        self.results["cases"]["01-runtime-identity"] = {
            "outcome": outcome,
            "runtime_executable_path": executable_target,
            "runtime_executable_sha256": executable_hash,
            "proc_root_executable_sha256": root_executable_hash,
            "uid": [1000] * 4,
            "gid": [1000] * 4,
            "capabilities_zero": True,
            "product_umask": "0077",
            "launch_umask": "0022",
            "uid_map": uid_map,
            "gid_map": gid_map,
            "cwd_matches_case_root": True,
            "environment_keys": sorted(env_map),
            "no_ld_library_path": True,
            "only_disclosed_preload": True,
            "host_backed_same_inode": True,
            "filesystem": "btrfs",
            "qt_library_basenames": [Path(path).name for path in qt_libraries],
            "bwrap_argv_sha256": sha256_bytes("\0".join(argv).encode()),
            "logs": logs,
            "no_open_deleted_log_fd": True,
        }

        normal = self.case_root("02-normal-pdf-lifecycle")
        fixture_before = sha256_file(self.fixture)
        result, normal_argv = self.run_app(normal, arguments=[str(self.fixture)], label="normal")
        check(result["return_code"] == 0 and result["elapsed_ms"] < 3000, "normal PDF lifecycle failed")
        verify_fixed(
            (normal / "normal-stderr.bin").read_bytes(),
            expected_events={"application-start": 1, "message-suppressed": 2},
        )
        logs = log_snapshot(self.log_directory(normal))
        combined = b"".join(
            (self.log_directory(normal) / name).read_bytes()
            for name in ("zenpdf.log.1", "zenpdf.log")
            if (self.log_directory(normal) / name).exists()
        )
        verify_fixed(combined, expected_events={"application-start": 1, "message-suppressed": 2})
        normal_diagnostics = (normal / "normal-stderr.bin").read_bytes() + combined
        check(str(self.fixture).encode() not in normal_diagnostics, "approved fixture path leaked")
        check(self.fixture.name.encode() not in normal_diagnostics, "approved fixture basename leaked")
        check(sha256_file(self.fixture) == fixture_before == FIXTURE_SHA256, "approved PDF changed")
        self.results["cases"]["02-normal-pdf-lifecycle"] = {
            "outcome": result,
            "qpa": "offscreen",
            "full_application_teardown": True,
            "destructor_warning_sanitized": True,
            "approved_fixture_path_and_basename_absent": True,
            "fixture_sha256_before_after": FIXTURE_SHA256,
            "logs": logs,
            "bwrap_argv_sha256": sha256_bytes("\0".join(normal_argv).encode()),
        }

    def hostile_log_directories(self) -> None:
        kinds = ("symlink", "fifo", "file", "unsafe-0777", "wrong-owner", "repair-0755")
        for kind in kinds:
            case_id = f"03-log-directory-{kind}"
            root = self.case_root(case_id)
            app_state = root / "data" / "ZenPDF" / "ZenPDF"
            app_state.mkdir(parents=True, mode=0o700)
            log_directory = app_state / "logs"
            target = root / "target"
            extra_binds: list[tuple[Path, Path, bool]] = []
            wrong_owner_inside: dict[str, Any] | None = None
            wrong_owner_after: dict[str, Any] | None = None
            if kind == "symlink":
                target.mkdir(mode=0o700)
                write_private(target / "sentinel", b"DIRECTORY-TARGET-SENTINEL\n")
                log_directory.symlink_to(target)
            elif kind == "fifo":
                os.mkfifo(log_directory, 0o600)
            elif kind == "file":
                write_private(log_directory, b"DIRECTORY-FILE-SENTINEL\n")
            elif kind == "unsafe-0777":
                log_directory.mkdir(mode=0o700)
                os.chmod(log_directory, 0o777)
            elif kind == "wrong-owner":
                log_directory.mkdir(mode=0o700)
                extra_binds.append((Path("/etc/credstore"), log_directory, False))
                wrong_owner_inside = self.inside_overlay_metadata(root, log_directory, extra_binds)
                check(not wrong_owner_inside["owner_matches_euid"], "wrong-owner directory control is not wrong-owner")
                check(wrong_owner_inside["mode"] == "0700", "wrong-owner directory mode is not otherwise valid")
            else:
                log_directory.mkdir(mode=0o700)
                os.chmod(log_directory, 0o755)
            before = metadata(log_directory)
            target_before = metadata(target) if target.exists() else None
            system_before = metadata(Path("/etc/credstore")) if kind == "wrong-owner" else None
            result, _ = self.run_app(root, extra_binds=extra_binds, label="hostile-dir")
            check(result["return_code"] == 0 and result["elapsed_ms"] < 1000, f"{kind} directory case not prompt")
            stderr_events = verify_fixed(
                (root / "hostile-dir-stderr.bin").read_bytes(),
                expected_events={"application-start": 1, "message-suppressed": 2},
            )
            after = metadata(log_directory)
            target_after = metadata(target) if target.exists() else None
            if kind == "repair-0755":
                check(after["type"] == "directory" and after["mode"] == "0700", "0755 directory was not safely repaired")
                logs = log_snapshot(log_directory)
            elif kind == "wrong-owner":
                wrong_owner_after = self.inside_overlay_metadata(root, log_directory, extra_binds)
                check(stable_metadata(before) == stable_metadata(after), "wrong-owner placeholder changed")
                check(stable_metadata(system_before or {}) == stable_metadata(metadata(Path("/etc/credstore"))), "wrong-owner bind target changed")
                check(wrong_owner_inside == wrong_owner_after, "wrong-owner inside directory changed")
                check(not list(log_directory.iterdir()), "wrong-owner placeholder gained child")
                logs = {
                    "inside_before": wrong_owner_inside,
                    "inside_after": wrong_owner_after,
                    "inside_unchanged": True,
                }
            else:
                check(stable_metadata(before) == stable_metadata(after), f"hostile {kind} directory changed")
                if target_before is not None:
                    check(stable_metadata(target_before) == stable_metadata(target_after or {}), "directory target changed")
                logs = {"entry": after}
            self.results["cases"][case_id] = {
                "outcome": result,
                "before": before,
                "after": after,
                "target_before": target_before,
                "target_after": target_after,
                "stderr": stderr_events,
                "logs": logs,
                "expected_mutation": "mode 0755 to 0700 plus private log" if kind == "repair-0755" else "none",
                "wrong_owner_inside_before": wrong_owner_inside,
                "wrong_owner_inside_after": wrong_owner_after,
                "wrong_owner_inside_unchanged": (
                    wrong_owner_inside == wrong_owner_after if kind == "wrong-owner" else None
                ),
            }

    def hostile_log_children(self) -> None:
        kinds = ("symlink", "hardlink", "fifo", "directory", "wrong-owner", "0666", "0644", "0400")
        for name in ("zenpdf.log", "zenpdf.log.1"):
            for kind in kinds:
                case_id = f"04-{name.replace('.', '-')}-{kind}"
                root = self.case_root(case_id)
                log_directory = self.log_directory(root)
                log_directory.mkdir(parents=True, mode=0o700)
                child = log_directory / name
                target = root / "target"
                extra_binds: list[tuple[Path, Path, bool]] = []
                if kind == "symlink":
                    write_private(target, b"CHILD-TARGET-SENTINEL\n")
                    child.symlink_to(target)
                elif kind == "hardlink":
                    write_private(target, b"CHILD-TARGET-SENTINEL\n")
                    os.link(target, child)
                elif kind == "fifo":
                    os.mkfifo(child, 0o600)
                elif kind == "directory":
                    child.mkdir(mode=0o700)
                elif kind == "wrong-owner":
                    write_private(child, b"WRONG-OWNER-PLACEHOLDER\n")
                    extra_binds.append((Path("/etc/pacman.d/gnupg/secring.gpg"), child, False))
                else:
                    write_private(child, b"CHILD-MODE-SENTINEL\n", int(kind, 8))
                before = metadata(child)
                target_before = metadata(target) if target.exists() else None
                hosts_before = metadata(Path("/etc/pacman.d/gnupg/secring.gpg")) if kind == "wrong-owner" else None
                wrong_owner_inside = (
                    self.inside_overlay_metadata(root, child, extra_binds)
                    if kind == "wrong-owner" else None
                )
                if wrong_owner_inside is not None:
                    check(not wrong_owner_inside["owner_matches_euid"], "wrong-owner child control is not wrong-owner")
                    check(wrong_owner_inside["mode"] == "0600", "wrong-owner child mode is not otherwise valid")
                    check(wrong_owner_inside["nlink"] == 1, "wrong-owner child link count is not otherwise valid")
                result, _ = self.run_app(root, extra_binds=extra_binds, label="hostile-child")
                check(result["return_code"] == 0 and result["elapsed_ms"] < 1000, f"{case_id} not prompt")
                stderr_events = verify_fixed(
                    (root / "hostile-child-stderr.bin").read_bytes(),
                    expected_events={"application-start": 1, "message-suppressed": 2},
                )
                after = metadata(child)
                check(stable_metadata(before) == stable_metadata(after), f"{case_id} entry changed")
                check(sorted(item.name for item in log_directory.iterdir()) == [name], f"{case_id} created unexpected log entry")
                if target_before is not None:
                    check(stable_metadata(target_before) == stable_metadata(metadata(target)), f"{case_id} target changed")
                if hosts_before is not None:
                    wrong_owner_after = self.inside_overlay_metadata(root, child, extra_binds)
                    check(stable_metadata(hosts_before) == stable_metadata(metadata(Path("/etc/pacman.d/gnupg/secring.gpg"))), "wrong-owner bind source changed")
                    check(wrong_owner_inside == wrong_owner_after, "wrong-owner inside child changed")
                self.results["cases"][case_id] = {
                    "outcome": result,
                    "entry_before": before,
                    "entry_after": after,
                    "target_before": target_before,
                    "target_after": metadata(target) if target.exists() else None,
                    "wrong_owner_inside_before": wrong_owner_inside,
                    "wrong_owner_inside_after": (
                        wrong_owner_after if kind == "wrong-owner" else None
                    ),
                    "wrong_owner_inside_unchanged": (
                        wrong_owner_inside == wrong_owner_after if kind == "wrong-owner" else None
                    ),
                    "stderr": stderr_events,
                    "directory_entries": [name],
                    "expected_mutation": "none",
                }

    def oversize_and_rotation(self) -> None:
        for kind in ("active-oversize", "rotated-oversize", "both-oversize", "active-exact-limit"):
            case_id = f"05-{kind}"
            root = self.case_root(case_id)
            log_directory = self.log_directory(root)
            log_directory.mkdir(parents=True, mode=0o700)
            active = log_directory / "zenpdf.log"
            rotated = log_directory / "zenpdf.log.1"
            filler_size = MAX_LOG_BYTES if kind == "active-exact-limit" else MAX_LOG_BYTES + 128
            filler, filler_counts = exact_filler(filler_size)
            if kind in {"active-oversize", "both-oversize", "active-exact-limit"}:
                write_private(active, filler)
            if kind in {"rotated-oversize", "both-oversize"}:
                write_private(rotated, filler)
            before = log_snapshot(log_directory, verify=False)
            result, _ = self.run_app(root, label="bounds")
            check(result["return_code"] == 0 and result["elapsed_ms"] < 3000, f"{kind} failed")
            verify_fixed(
                (root / "bounds-stderr.bin").read_bytes(),
                expected_events={"application-start": 1, "message-suppressed": 2},
            )
            after = log_snapshot(log_directory)
            names = sorted(after["entries"])
            if kind == "active-exact-limit":
                check(names == ["zenpdf.log", "zenpdf.log.1"], "exact-limit case did not rotate")
                check(after["entries"]["zenpdf.log.1"]["size"] == MAX_LOG_BYTES, "rotated exact-limit size changed")
                check(after["entries"]["zenpdf.log.1"]["sha256"] == sha256_bytes(filler), "exact-limit filler changed")
                active_events = after["entries"]["zenpdf.log"]["events"]["event_counts"]
                check(active_events == {"application-start": 1, "message-suppressed": 2}, "exact-limit active events mismatch")
            else:
                check(names == ["zenpdf.log"], f"oversized {kind} file survived")
                active_events = after["entries"]["zenpdf.log"]["events"]["event_counts"]
                check(active_events == {"application-start": 1, "message-suppressed": 2}, "fresh events mismatch")
                check(after["entries"]["zenpdf.log"]["sha256"] != sha256_bytes(filler), "oversized contents retained")
            self.results["cases"][case_id] = {
                "outcome": result,
                "filler_size": filler_size,
                "filler_sha256": sha256_bytes(filler),
                "filler_event_counts": filler_counts,
                "before": before,
                "after": after,
            }

    def multiprocess(self) -> None:
        case_id = "06-multiprocess-default-rotation"
        root = self.case_root(case_id)
        shared_logs = root / "shared-logs"
        shared_logs.mkdir(mode=0o700)
        filler_size = 900 * 1024
        filler, filler_counts = exact_filler(filler_size)
        write_private(shared_logs / "zenpdf.log", filler)
        process_count = 8
        event_count = 1000
        running: list[dict[str, Any]] = []
        for index in range(process_count):
            process_root = root / f"process-{index:02d}"
            process_root.mkdir(mode=0o700)
            for directory in ("home", "data", "config", "cache", "state", "runtime"):
                (process_root / directory).mkdir(mode=0o700)
            process_log = self.log_directory(process_root)
            process_log.mkdir(parents=True, mode=0o700)
            ready = process_root / "ready"
            trigger = process_root / "trigger"
            process, stdout, stderr, argv, started = self.start_app(
                process_root,
                mode="multi",
                extra_env={
                    "ZENPDF_L005_COUNT": str(event_count),
                    "ZENPDF_L005_READY": str(ready),
                    "ZENPDF_L005_TRIGGER": str(trigger),
                },
                extra_binds=[(shared_logs, process_log, True)],
                label="multi",
            )
            running.append({
                "process": process,
                "stdout": stdout,
                "stderr": stderr,
                "argv": argv,
                "started": started,
                "ready": ready,
                "trigger": trigger,
                "process_log": process_log,
            })
        app_pids: list[int] = []
        for item in running:
            wait_for_path(item["ready"], 15.0)
            app_pid = find_app_pid(item["process"].pid)
            app_pids.append(app_pid)
            check(not deleted_log_descriptors(app_pid), "multi process has open-deleted log fd")
            shared_stat = shared_logs.stat()
            process_stat = Path(f"/proc/{app_pid}/root{item['process_log']}").stat()
            check(shared_stat.st_dev == process_stat.st_dev and shared_stat.st_ino == process_stat.st_ino, "multi process does not share exact log directory inode")
        for item in running:
            item["trigger"].touch(mode=0o600)
        outcomes = []
        for item in running:
            outcome = self.finish_app(
                item["process"], item["stdout"], item["stderr"], item["started"], timeout=15.0
            )
            check(outcome["return_code"] == 0, "multi process failed")
            verify_fixed(
                item["stderr"].read_bytes(),
                expected_events={"application-start": 1, "message-suppressed": event_count},
            )
            outcomes.append(outcome)
        after = log_snapshot(shared_logs)
        check(sorted(after["entries"]) == ["zenpdf.log", "zenpdf.log.1"], "multi process did not retain exact two-file set")
        combined = (shared_logs / "zenpdf.log.1").read_bytes() + (shared_logs / "zenpdf.log").read_bytes()
        events = verify_fixed(combined)
        expected = {
            "application-start": filler_counts.get("application-start", 0) + process_count,
            "message-suppressed": filler_counts.get("message-suppressed", 0) + process_count * event_count,
        }
        check(events["event_counts"] == expected, f"multi-process retained counts mismatch: {events['event_counts']!r} != {expected!r}")
        check(not any(deleted_log_descriptors(pid) for pid in app_pids if Path(f"/proc/{pid}").exists()), "open-deleted log fd remains")
        self.results["cases"][case_id] = {
            "process_count": process_count,
            "events_per_process": event_count,
            "filler_size": filler_size,
            "filler_sha256": sha256_bytes(filler),
            "filler_event_counts": filler_counts,
            "expected_retained_events": expected,
            "observed_retained_events": events["event_counts"],
            "same_host_backed_directory_inode": True,
            "no_open_deleted_log_fd_while_waiting": True,
            "outcomes": outcomes,
            "logs": after,
        }

    def contention(self) -> None:
        setup_id = "07-setup-contention-recovery"
        root = self.case_root(setup_id)
        log_directory = self.log_directory(root)
        log_directory.mkdir(parents=True, mode=0o700)
        before = log_snapshot(log_directory)
        holder = os.open(log_directory, os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC)
        fcntl.flock(holder, fcntl.LOCK_EX)
        ready = root / "ready"
        trigger = root / "trigger"
        process, stdout, stderr, argv, started = self.start_app(
            root,
            mode="setup",
            extra_env={"ZENPDF_L005_READY": str(ready), "ZENPDF_L005_TRIGGER": str(trigger)},
            label="setup-contention",
        )
        wait_started = time.monotonic_ns()
        wait_for_path(ready, 1.0)
        ready_ms = (time.monotonic_ns() - wait_started) / 1_000_000
        during = log_snapshot(log_directory)
        check(stable_metadata(before) == stable_metadata(during), "setup contention mutated log directory")
        fcntl.flock(holder, fcntl.LOCK_UN)
        os.close(holder)
        trigger.touch(mode=0o600)
        result = self.finish_app(process, stdout, stderr, started)
        check(result["return_code"] == 0 and ready_ms < 1000, "setup contention not bounded")
        verify_fixed(stderr.read_bytes(), expected_events={"application-start": 1, "message-suppressed": 1})
        after = log_snapshot(log_directory)
        log_events = after["entries"]["zenpdf.log"]["events"]["event_counts"]
        check(log_events == {"message-suppressed": 1}, "setup recovery did not append exactly one event")
        self.results["cases"][setup_id] = {
            "ready_under_lock_ms": round(ready_ms, 3),
            "outcome": result,
            "before": before,
            "during": during,
            "after": after,
            "same_process_recovery": True,
            "bwrap_argv_sha256": sha256_bytes("\0".join(argv).encode()),
        }

        runtime_id = "08-runtime-contention-recovery"
        root = self.case_root(runtime_id)
        ready = root / "ready"
        emitted = root / "emitted"
        trigger_one = root / "trigger-one"
        trigger_two = root / "trigger-two"
        process, stdout, stderr, argv, started = self.start_app(
            root,
            mode="runtime",
            extra_env={
                "ZENPDF_L005_READY": str(ready),
                "ZENPDF_L005_EMITTED": str(emitted),
                "ZENPDF_L005_TRIGGER_ONE": str(trigger_one),
                "ZENPDF_L005_TRIGGER_TWO": str(trigger_two),
            },
            label="runtime-contention",
        )
        wait_for_path(ready, 3.0)
        log_directory = self.log_directory(root)
        before = log_snapshot(log_directory)
        holder = os.open(log_directory, os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC)
        fcntl.flock(holder, fcntl.LOCK_EX)
        trigger_one.touch(mode=0o600)
        wait_started = time.monotonic_ns()
        wait_for_path(emitted, 1.0)
        emitted_ms = (time.monotonic_ns() - wait_started) / 1_000_000
        during = log_snapshot(log_directory)
        check(before == during, "runtime contention mutated log")
        fcntl.flock(holder, fcntl.LOCK_UN)
        os.close(holder)
        trigger_two.touch(mode=0o600)
        result = self.finish_app(process, stdout, stderr, started)
        check(result["return_code"] == 0 and emitted_ms < 1000, "runtime contention not bounded")
        verify_fixed(stderr.read_bytes(), expected_events={"application-start": 1, "message-suppressed": 2})
        after = log_snapshot(log_directory)
        events = after["entries"]["zenpdf.log"]["events"]["event_counts"]
        check(events == {"application-start": 1, "message-suppressed": 1}, "runtime recovery event count mismatch")
        self.results["cases"][runtime_id] = {
            "contended_event_ms": round(emitted_ms, 3),
            "outcome": result,
            "before": before,
            "during": during,
            "after": after,
            "same_process_recovery": True,
            "bwrap_argv_sha256": sha256_bytes("\0".join(argv).encode()),
        }

    @staticmethod
    def coredumps() -> tuple[list[dict[str, Any]], str]:
        result = command(
            ["coredumpctl", "--no-pager", "--json=short", "list", "/usr/bin/zenpdf", "--since", "@0"],
            timeout=10,
            check_status=False,
        )
        try:
            values = json.loads(result.stdout or b"[]")
        except json.JSONDecodeError as error:
            raise AssertionError(f"coredump JSON invalid: {result.stdout[-2048:]!r}") from error
        return values, result.stderr.decode("utf-8", "replace")

    def fatal_and_fstat(self) -> None:
        case_id = "09-fatal-contention-restart-no-core"
        root = self.case_root(case_id)
        log_directory = self.log_directory(root)
        log_directory.mkdir(parents=True, mode=0o700)
        initial, initial_counts = exact_filler(len(FILLER_LINES[0]))
        write_private(log_directory / "zenpdf.log", initial)
        before_dumps, coredump_stderr_before = self.coredumps()
        start_wall_us = time.time_ns() // 1000
        holder = os.open(log_directory, os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC)
        fcntl.flock(holder, fcntl.LOCK_EX)
        fatal_name = "l005-" + secrets.token_hex(4)
        check(len(fatal_name) <= 15, "fatal correlation marker is too long")
        ready = root / "ready"
        trigger = root / "trigger"
        process, stdout, stderr, argv, started = self.start_app(
            root,
            mode="fatal-wait",
            extra_env={
                "ZENPDF_L005_READY": str(ready),
                "ZENPDF_L005_TRIGGER": str(trigger),
                "ZENPDF_L005_FATAL_NAME": fatal_name,
            },
            label="fatal",
        )
        wait_started = time.monotonic_ns()
        wait_for_path(ready, 1.0)
        ready_ms = (time.monotonic_ns() - wait_started) / 1_000_000
        app_pid = find_app_pid(process.pid)
        inner_pid = app_pid
        live_comm = Path(f"/proc/{app_pid}/comm").read_text().strip()
        check(live_comm == fatal_name, "live fatal correlation marker mismatch")
        check(not deleted_log_descriptors(app_pid), "fatal process has open-deleted log fd")
        before = log_snapshot(log_directory)
        trigger_wall_us = time.time_ns() // 1000
        trigger.touch(mode=0o600)
        fatal_started = time.monotonic_ns()
        result = self.finish_app(process, stdout, stderr, started, timeout=1.0)
        process_end_wall_us = time.time_ns() // 1000
        fatal_ms = (time.monotonic_ns() - fatal_started) / 1_000_000
        during = log_snapshot(log_directory)
        check(before == during, "fatal under contention mutated log")
        check(result["return_code"] in {-signal.SIGABRT, 128 + signal.SIGABRT}, f"fatal status is not SIGABRT: {result['return_code']}")
        check(ready_ms < 1000 and fatal_ms < 1000, "fatal contention not bounded")
        stderr_events = verify_fixed(stderr.read_bytes(), expected_events={"application-start": 1, "message-suppressed": 1})
        check(stderr_events["severity_counts"].get("fatal") == 1, "exact fatal event missing")
        fcntl.flock(holder, fcntl.LOCK_UN)
        os.close(holder)

        restart, restart_argv = self.run_app(root, label="fatal-restart")
        check(restart["return_code"] == 0 and restart["elapsed_ms"] < 3000, "fatal restart failed")
        verify_fixed(
            (root / "fatal-restart-stderr.bin").read_bytes(),
            expected_events={"application-start": 1, "message-suppressed": 2},
        )
        after = log_snapshot(log_directory)
        expected_after = {
            "application-start": initial_counts.get("application-start", 0) + 1,
            "message-suppressed": initial_counts.get("message-suppressed", 0) + 2,
        }
        check(after["entries"]["zenpdf.log"]["events"]["event_counts"] == expected_after, "restart retained event mismatch")

        bounded_candidates: list[dict[str, Any]] = []
        marker_matches: list[dict[str, Any]] = []
        coredump_stderr_after = ""
        deadline = time.monotonic() + 5.0
        while time.monotonic() < deadline:
            dumps, coredump_stderr_after = self.coredumps()
            bounded_candidates = [
                item for item in dumps
                if item.get("exe") == "/usr/bin/zenpdf"
                and item.get("sig") == signal.SIGABRT
                and item.get("uid") == 1000
                and item.get("gid") == 1000
                and trigger_wall_us <= int(item.get("time", 0)) <= process_end_wall_us
            ]
            marker_matches = []
            for candidate in bounded_candidates:
                info_result = command(
                    [
                        "coredumpctl", "--no-pager", "--json=short", "info",
                        str(candidate["pid"]),
                    ],
                    timeout=10,
                    check_status=False,
                )
                try:
                    info = json.loads(info_result.stdout)
                except json.JSONDecodeError:
                    continue
                if info.get("ThreadName") == fatal_name:
                    marker_matches.append(info)
            if marker_matches:
                break
            time.sleep(0.1)
        check(len(marker_matches) == 1, f"expected one marker-matched coredump entry, got {marker_matches!r}")
        info = marker_matches[0]
        check(info.get("PID") in [item.get("pid") for item in bounded_candidates], "coredump info/list PID mismatch")
        check(info.get("ThreadName") == fatal_name, "coredump thread marker mismatch")
        check(info.get("CommandLine") == "/usr/bin/zenpdf", "coredump command line mismatch")
        check(info.get("Executable") == "/usr/bin/zenpdf", "coredump executable mismatch")
        check(info.get("UID") == 1000 and info.get("GID") == 1000, "coredump identity mismatch")
        check(info.get("Signal") == signal.SIGABRT, "coredump signal mismatch")
        check(info.get("Storage") == "none", f"fatal stored a core: {info!r}")
        matched_candidate = next(
            item for item in bounded_candidates if item.get("pid") == info.get("PID")
        )
        check(
            trigger_wall_us <= int(matched_candidate["time"]) <= process_end_wall_us,
            "coredump list timestamp is outside the fatal window",
        )
        allowlisted_info = {
            key: info.get(key)
            for key in (
                "PID", "ThreadName", "CommandLine", "Executable", "UID", "GID",
                "Signal", "Storage", "Timestamp",
            )
        }
        check(not any(path.name == "core" or path.name.startswith("core.") for path in root.rglob("*")), "task root contains core file")
        self.results["cases"][case_id] = {
            "outcome": result,
            "ready_under_lock_ms": round(ready_ms, 3),
            "fatal_after_trigger_ms": round(fatal_ms, 3),
            "inner_pid": inner_pid,
            "acceptance_window_start_us": start_wall_us,
            "fatal_marker": fatal_name,
            "live_comm": live_comm,
            "trigger_wall_us": trigger_wall_us,
            "process_end_wall_us": process_end_wall_us,
            "coredump_host_pid": info.get("PID"),
            "pid_correlation": "unique live PR_SET_NAME marker plus bounded executable/signal/identity/time-window journal entry",
            "coredump_bounded_candidate_count": len(bounded_candidates),
            "coredump_marker_match_count": len(marker_matches),
            "coredump_list_entry": {
                key: matched_candidate.get(key)
                for key in ("time", "pid", "uid", "gid", "sig", "corefile", "exe", "size")
            },
            "coredump_info": allowlisted_info,
            "preexisting_coredump_count_preserved": len(before_dumps),
            "coredumpctl_bus_warning_before": coredump_stderr_before.strip(),
            "coredumpctl_bus_warning_after": coredump_stderr_after.strip(),
            "during": during,
            "after_restart": after,
            "restart_outcome": restart,
            "restart_same_root": True,
            "no_stored_core": True,
            "no_open_deleted_log_fd": True,
            "fatal_bwrap_argv_sha256": sha256_bytes("\0".join(argv).encode()),
            "restart_bwrap_argv_sha256": sha256_bytes("\0".join(restart_argv).encode()),
        }

        fault_id = "10-fstat-size-inspection-fail-closed"
        root = self.case_root(fault_id)
        log_directory = self.log_directory(root)
        log_directory.mkdir(parents=True, mode=0o700)
        active = log_directory / "zenpdf.log"
        filler, filler_counts = exact_filler(MAX_LOG_BYTES)
        write_private(active, filler)
        before = log_snapshot(log_directory)
        result, argv = self.run_app(
            root,
            mode="none",
            extra_env={"ZENPDF_L005_FAIL_LOG_FSTAT": "3"},
            use_fstat_probe=True,
            label="fstat-fault",
        )
        check(result["return_code"] == 0 and result["elapsed_ms"] < 3000, "fstat fault process failed")
        verify_fixed(
            (root / "fstat-fault-stderr.bin").read_bytes(),
            expected_events={"application-start": 1},
        )
        after = log_snapshot(log_directory)
        check(before == after, "fstat size-inspection failure mutated log")
        check(not (log_directory / "zenpdf.log.1").exists(), "fstat failure unexpectedly rotated")
        restart, _ = self.run_app(root, label="fstat-restart")
        check(restart["return_code"] == 0, "fstat recovery failed")
        final = log_snapshot(log_directory)
        check(sorted(final["entries"]) == ["zenpdf.log", "zenpdf.log.1"], "fstat recovery did not rotate exact-limit log")
        check(final["entries"]["zenpdf.log.1"]["sha256"] == sha256_bytes(filler), "fstat recovery changed rotated filler")
        self.results["cases"][fault_id] = {
            "outcome": result,
            "filler_size": MAX_LOG_BYTES,
            "filler_sha256": sha256_bytes(filler),
            "filler_event_counts": filler_counts,
            "before": before,
            "after_fault": after,
            "after_recovery": final,
            "recovery_outcome": restart,
            "bwrap_argv_sha256": sha256_bytes("\0".join(argv).encode()),
        }

    def rollback_and_closeout(self) -> None:
        pacman_prefix = [
            "unshare", "--user", "--map-root-user",
            "pacman", "--root", str(self.package_root),
            "--dbpath", str(self.package_root / "var/lib/pacman"),
            "--config", str(self.package_root / "etc/pacman.conf"),
        ]
        rollback = command(
            pacman_prefix + ["-U", "--noconfirm", "--nodeps", "--nodeps", str(self.rollback_package)],
            timeout=30,
        )
        rollback_query = command(pacman_prefix + ["-Q", "zenpdf-git"]).stdout.decode().strip()
        rollback_qkk_result = command(pacman_prefix + ["-Qkk", "zenpdf-git"])
        rollback_qkk = (rollback_qkk_result.stdout + rollback_qkk_result.stderr).decode()
        check(rollback_query == "zenpdf-git 0.1.0.r0.geeba33d-1", "namespace rollback version mismatch")
        check("zenpdf-git: 12 total files, 0 altered files" in rollback_qkk, "namespace rollback Qkk mismatch")

        reinstall = command(
            pacman_prefix + ["-U", "--noconfirm", "--nodeps", "--nodeps", str(self.artifact_package)],
            timeout=30,
        )
        reinstall_query = command(pacman_prefix + ["-Q", "zenpdf-git"]).stdout.decode().strip()
        reinstall_qkk_result = command(pacman_prefix + ["-Qkk", "zenpdf-git"])
        reinstall_qkk = (reinstall_qkk_result.stdout + reinstall_qkk_result.stderr).decode()
        check(reinstall_query == f"zenpdf-git {PACKAGE_VERSION}", "namespace candidate reinstall version mismatch")
        check("zenpdf-git: 12 total files, 0 altered files" in reinstall_qkk, "namespace candidate reinstall Qkk mismatch")
        check(sha256_file(self.candidate) == EXECUTABLE_SHA256, "candidate payload not restored after rollback test")

        source_head = command(["git", "-C", str(self.source_clone), "rev-parse", "HEAD"]).stdout.decode().strip()
        source_tree = command(["git", "-C", str(self.source_clone), "rev-parse", "HEAD^{tree}"]).stdout.decode().strip()
        source_status = command(["git", "-C", str(self.source_clone), "status", "--porcelain=v1"]).stdout.decode()
        check(source_head == IMPLEMENTATION_SHA and source_tree == IMPLEMENTATION_TREE and not source_status, "source changed during acceptance")
        check(sha256_file(self.fixture) == FIXTURE_SHA256, "fixture changed during acceptance")
        host_query = command(["pacman", "-Q", "zenpdf-git"]).stdout.decode().strip()
        host_hash = sha256_file(Path("/usr/bin/zenpdf"))
        check(host_query == f"zenpdf-git {HOST_PACKAGE_VERSION}" and host_hash == HOST_EXECUTABLE_SHA256, "host production package changed")

        residual_exact: list[int] = []
        deleted_fds: list[dict[str, Any]] = []
        for proc in Path("/proc").iterdir():
            if not proc.name.isdigit():
                continue
            pid = int(proc.name)
            try:
                if sha256_file(proc / "exe") == EXECUTABLE_SHA256:
                    residual_exact.append(pid)
            except (FileNotFoundError, PermissionError, OSError):
                pass
            try:
                for fd in (proc / "fd").iterdir():
                    target = os.readlink(fd)
                    if "zenpdf.log" in target and target.endswith(" (deleted)"):
                        deleted_fds.append({"pid": pid, "target_sha256": sha256_bytes(target.encode())})
            except (FileNotFoundError, PermissionError, OSError):
                pass
        check(not residual_exact, f"exact candidate residual processes: {residual_exact!r}")
        check(not deleted_fds, f"open-deleted log descriptors remain: {deleted_fds!r}")

        roots = sorted(path.name for path in self.run.iterdir())
        all_runs = sorted(path.name for path in (self.root / "cases").iterdir())
        all_record_runs = sorted(path.name for path in (self.root / "records").iterdir())
        self.results["closeout"] = {
            "source_sha": source_head,
            "source_tree": source_tree,
            "source_clean": True,
            "fixture_sha256": FIXTURE_SHA256,
            "rollback_sha256": sha256_file(self.rollback_package),
            "namespace_rollback": {
                "version": rollback_query,
                "qkk_12_total_0_altered": True,
                "pacman_output_sha256": sha256_bytes(rollback.stdout + rollback.stderr),
            },
            "namespace_candidate_reinstall": {
                "version": reinstall_query,
                "qkk_12_total_0_altered": True,
                "payload_sha256": sha256_file(self.candidate),
                "pacman_output_sha256": sha256_bytes(reinstall.stdout + reinstall.stderr),
            },
            "host_production_package_untouched": {
                "version": host_query,
                "executable_sha256": host_hash,
            },
            "exact_candidate_residual_processes": 0,
            "open_deleted_log_descriptors": 0,
            "task_case_roots": roots,
            "all_acceptance_run_roots": all_runs,
            "all_acceptance_record_roots": all_record_runs,
            "no_production_deletion": True,
        }

    def run_all(self) -> Path:
        try:
            self.preflight()
            self.positive_filesystem_controls()
            self.identity_and_normal()
            self.hostile_log_directories()
            self.hostile_log_children()
            self.oversize_and_rotation()
            self.multiprocess()
            self.contention()
            self.fatal_and_fstat()
            self.rollback_and_closeout()
            self.results["completed_utc"] = dt.datetime.now(dt.timezone.utc).isoformat()
            self.results["result"] = "PASS"
            result_path = self.records / "l005-installed-acceptance.json"
            write_json(result_path, self.results)
            return result_path
        except Exception as error:
            self.results["completed_utc"] = dt.datetime.now(dt.timezone.utc).isoformat()
            self.results["result"] = "FAIL"
            self.results["failure"] = repr(error)
            write_json(self.records / "l005-installed-acceptance-failed.json", self.results)
            raise
        finally:
            self.close()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run exact-package L005 installed acceptance")
    parser.add_argument("--acceptance-root", type=Path, required=True)
    parser.add_argument("--package-root", type=Path, required=True)
    parser.add_argument("--source-clone", type=Path, required=True)
    parser.add_argument("--rollback-package", type=Path, required=True)
    return parser.parse_args()


def main() -> int:
    acceptance = Acceptance(parse_args())
    result = acceptance.run_all()
    print(result)
    return 0


if __name__ == "__main__":
    sys.exit(main())
