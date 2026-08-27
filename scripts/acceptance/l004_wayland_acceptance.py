#!/usr/bin/env python3
"""Run the installed-package L004 acceptance gate on a real Hyprland session."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shlex
import shutil
import signal
import stat
import subprocess
import sys
import time
from pathlib import Path
from typing import Any, Callable


CLIENT_CLASS = "io.github.rohan-patnaik.zenpdf"
MAIN_TITLE = "ZenPDF"
FAILURE_TITLE = "Window preferences not saved"
MAX_CAPTURE_BYTES = 64 * 1024
BACKGROUND_LAUNCHER = Path("/home/rohan/.local/bin/codex-background-launch")
ACTIVE_RUNS: list["ManagedProcess"] = []
ACTIVE_NESTED_RUNS: list[tuple[subprocess.Popen[bytes], Any]] = []
NESTED_SIZE = (1280, 720)


class ManagedProcess:
    def __init__(self, pid: int, status_path: Path) -> None:
        self.pid = pid
        self.status_path = status_path

    def poll(self) -> int | None:
        if not self.status_path.exists():
            return None
        return int(self.status_path.read_text(encoding="ascii").strip())

    def wait(self, timeout: float) -> int:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            status = self.poll()
            if status is not None:
                return status
            time.sleep(0.05)
        raise subprocess.TimeoutExpired("background ZenPDF", timeout)


class NestedWayland:
    def __init__(self, root: Path) -> None:
        self.runtime = root / "nested-runtime"
        self.runtime.mkdir(mode=0o700)
        self.socket = "wayland-zenpdf-l004"
        self.log = root / "weston.log"
        environment = os.environ.copy()
        environment["XDG_RUNTIME_DIR"] = str(self.runtime)
        self.process = subprocess.Popen(
            [
                "/usr/bin/weston",
                "--backend=headless",
                "--renderer=pixman",
                f"--width={NESTED_SIZE[0]}",
                f"--height={NESTED_SIZE[1]}",
                "--fake-seat",
                f"--socket={self.socket}",
                "--idle-time=0",
                "--no-config",
                f"--log={self.log}",
            ],
            stdin=subprocess.DEVNULL,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            env=environment,
            process_group=0,
        )
        try:
            wait_until(
                lambda: (self.runtime / self.socket).exists(),
                "headless Weston socket did not appear",
            )
        except BaseException:
            self.close()
            raise

    def environment(self, case_root: Path) -> dict[str, str]:
        environment = app_environment(case_root)
        environment["XDG_RUNTIME_DIR"] = str(self.runtime)
        environment["WAYLAND_DISPLAY"] = self.socket
        return environment

    def close(self) -> None:
        if self.process.poll() is None:
            try:
                os.killpg(self.process.pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
            try:
                self.process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                try:
                    os.killpg(self.process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                self.process.wait(timeout=2)
        if self.log.exists():
            os.chmod(self.log, 0o600)
            check(self.log.stat().st_size <= MAX_CAPTURE_BYTES, "nested compositor log exceeded 64 KiB")


def check(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def command(argv: list[str], timeout: float = 10.0) -> subprocess.CompletedProcess[bytes]:
    result = subprocess.run(argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=timeout)
    if result.returncode != 0:
        raise AssertionError(
            f"command failed ({result.returncode}): {argv[0]}: "
            f"{result.stderr[-1024:].decode('utf-8', 'replace')}"
        )
    return result


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def metadata(path: Path) -> dict[str, Any]:
    value = path.lstat()
    return {
        "regular": stat.S_ISREG(value.st_mode),
        "directory": stat.S_ISDIR(value.st_mode),
        "effective_uid_owned": value.st_uid == os.geteuid(),
        "mode": f"{stat.S_IMODE(value.st_mode):04o}",
        "links": value.st_nlink,
        "size": value.st_size,
        "sha256": sha256(path) if stat.S_ISREG(value.st_mode) else None,
    }


def identity(path: Path) -> tuple[int, int, int, int]:
    value = path.lstat()
    return value.st_dev, value.st_ino, value.st_mode, value.st_size


def directory_snapshot(path: Path) -> dict[str, Any]:
    check(path.is_dir() and not path.is_symlink(), "snapshot target is not a directory")
    entries: dict[str, Any] = {}
    for child in sorted(path.iterdir(), key=lambda item: item.name):
        check("/" not in child.name and child.name not in {".", ".."}, "unsafe entry name")
        entries[child.name] = metadata(child)
    return {"directory": metadata(path), "entries": entries}


def clients() -> list[dict[str, Any]]:
    return json.loads(command(["hyprctl", "clients", "-j"]).stdout)


def active_window_identity() -> tuple[Any, ...]:
    window = json.loads(command(["hyprctl", "activewindow", "-j"]).stdout)
    workspace = window.get("workspace") if isinstance(window.get("workspace"), dict) else {}
    return (
        window.get("address"),
        window.get("pid"),
        window.get("class"),
        window.get("title"),
        workspace.get("id"),
        workspace.get("name"),
    )


def wait_until(predicate: Callable[[], Any], message: str, timeout: float = 8.0) -> Any:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        value = predicate()
        if value:
            return value
        time.sleep(0.05)
    raise AssertionError(message)


def client_for(pid: int, title: str) -> dict[str, Any] | None:
    matches = [
        item for item in clients()
        if item.get("pid") == pid and item.get("class") == CLIENT_CLASS
        and item.get("title") == title
    ]
    check(len(matches) <= 1, f"multiple {title!r} clients found")
    return matches[0] if matches else None


def validate_address(address: str) -> str:
    check(re.fullmatch(r"0x[0-9a-f]+", address) is not None, "invalid compositor address")
    return address


def dispatch(expression: str) -> None:
    command(["hyprctl", "dispatch", expression])


def close_client(client: dict[str, Any]) -> None:
    address = validate_address(str(client["address"]))
    dispatch(f'hl.dsp.window.close({{ window = "address:{address}" }})')


def send_shortcut(client: dict[str, Any], key: str) -> None:
    check(re.fullmatch(r"[A-Z0-9]+", key) is not None, "invalid shortcut key")
    address = validate_address(str(client["address"]))
    dispatch(
        'hl.dsp.send_shortcut({ mods = "", '
        f'key = "{key}", window = "address:{address}" }})'
    )


def send_escape(dialog: dict[str, Any]) -> None:
    send_shortcut(dialog, "ESCAPE")


def app_environment(case_root: Path) -> dict[str, str]:
    environment = os.environ.copy()
    environment.update(
        {
            "XDG_DATA_HOME": str(case_root / "data"),
            "XDG_CONFIG_HOME": str(case_root / "config"),
            "XDG_CACHE_HOME": str(case_root / "cache"),
            "XDG_STATE_HOME": str(case_root / "state"),
            "QT_QPA_PLATFORM": "wayland",
            "QT_LINUX_ACCESSIBILITY_ALWAYS_ON": "1",
            "QT_ACCESSIBILITY": "1",
        }
    )
    return environment


def launch(executable: Path, case_root: Path, capture_name: str) -> ManagedProcess:
    capture_path = case_root / capture_name
    helper_path = case_root / f".{capture_name}.launch"
    pid_path = case_root / f".{capture_name}.pid"
    status_path = case_root / f".{capture_name}.status"
    environment = app_environment(case_root)
    names = (
        "DBUS_SESSION_BUS_ADDRESS",
        "WAYLAND_DISPLAY",
        "XDG_RUNTIME_DIR",
        "XDG_DATA_HOME",
        "XDG_CONFIG_HOME",
        "XDG_CACHE_HOME",
        "XDG_STATE_HOME",
        "QT_QPA_PLATFORM",
        "QT_LINUX_ACCESSIBILITY_ALWAYS_ON",
        "QT_ACCESSIBILITY",
    )
    separator = " " + "\\" + "\n  "
    assignments = separator.join(
        f"{name}={shlex.quote(environment[name])}"
        for name in names
        if environment.get(name)
    )
    helper = f"""#!/bin/sh
set -u
umask 077
setsid env \\
  {assignments} \\
  {shlex.quote(str(executable))} >{shlex.quote(str(capture_path))} 2>&1 &
child=$!
printf '%s\\n' "$child" >{shlex.quote(str(pid_path))}.tmp
mv {shlex.quote(str(pid_path))}.tmp {shlex.quote(str(pid_path))}
wait "$child"
status=$?
printf '%s\\n' "$status" >{shlex.quote(str(status_path))}.tmp
mv {shlex.quote(str(status_path))}.tmp {shlex.quote(str(status_path))}
exit 0
"""
    write_private(helper_path, helper.encode("utf-8"))
    os.chmod(helper_path, 0o700)
    command([str(BACKGROUND_LAUNCHER), str(helper_path)])
    wait_until(pid_path.exists, "background launcher did not report the ZenPDF pid")
    pid = int(pid_path.read_text(encoding="ascii").strip())
    check(pid > 1, "background launcher reported an invalid ZenPDF pid")
    process = ManagedProcess(pid, status_path)
    ACTIVE_RUNS.append(process)
    return process


def wait_for_main(process: ManagedProcess) -> dict[str, Any]:
    client = wait_until(
        lambda: client_for(process.pid, MAIN_TITLE),
        "installed ZenPDF did not map its main Wayland client",
    )
    check(client.get("mapped") is True, "main client is not mapped")
    check(client.get("hidden") is False, "main client is hidden")
    check(client.get("xwayland") is False, "client is not native Wayland")
    return client


def wait_for_usable_main(process: ManagedProcess) -> dict[str, Any]:
    def usable() -> dict[str, Any] | None:
        client = client_for(process.pid, MAIN_TITLE)
        if (
            client is not None
            and client.get("mapped") is True
            and client.get("hidden") is False
            and client.get("visible") is True
            and client.get("acceptsInput") is True
        ):
            return client
        return None

    return wait_until(usable, "main Wayland client did not become mapped and usable")


def wait_for_exit(process: ManagedProcess) -> int:
    status = process.wait(timeout=8)
    if process in ACTIVE_RUNS:
        ACTIVE_RUNS.remove(process)
    return status


def cleanup_active_runs() -> None:
    for process in ACTIVE_RUNS.copy():
        if process.poll() is None:
            try:
                os.killpg(process.pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
            try:
                process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                process.wait(timeout=2)
        ACTIVE_RUNS.remove(process)
    for process, capture in ACTIVE_NESTED_RUNS.copy():
        if process.poll() is None:
            try:
                os.killpg(process.pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
            try:
                process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                process.wait(timeout=2)
        capture.close()
        ACTIVE_NESTED_RUNS.remove((process, capture))


def launch_nested(
    executable: Path,
    case_root: Path,
    capture_name: str,
    nested: NestedWayland,
) -> tuple[subprocess.Popen[bytes], Any]:
    capture_path = case_root / capture_name
    capture = capture_path.open("wb")
    os.chmod(capture_path, 0o600)
    process = subprocess.Popen(
        [str(executable)],
        stdin=subprocess.DEVNULL,
        stdout=capture,
        stderr=subprocess.STDOUT,
        env=nested.environment(case_root),
        process_group=0,
    )
    ACTIVE_NESTED_RUNS.append((process, capture))
    return process, capture


def wait_for_nested_exit(process: subprocess.Popen[bytes], capture: Any) -> int:
    status = process.wait(timeout=8)
    capture.close()
    if (process, capture) in ACTIVE_NESTED_RUNS:
        ACTIVE_NESTED_RUNS.remove((process, capture))
    return status


def zenpdf_processes() -> list[int]:
    found: list[int] = []
    for item in Path("/proc").iterdir():
        if not item.name.isdigit():
            continue
        try:
            if (item / "comm").read_text(encoding="utf-8").strip() == "zenpdf":
                found.append(int(item.name))
        except (FileNotFoundError, PermissionError, ProcessLookupError):
            continue
    return found


def bounded_capture(case_root: Path, name: str) -> dict[str, Any]:
    path = case_root / name
    value = path.read_bytes()
    check(len(value) <= MAX_CAPTURE_BYTES, "installed application output exceeded 64 KiB")
    check(os.fsencode(case_root.name) not in value, "application output disclosed the case root")
    return {"bytes": len(value), "sha256": hashlib.sha256(value).hexdigest()}


def strip_schema(serialized: bytes) -> bytes:
    lines = serialized.splitlines(keepends=True)
    output: list[bytes] = []
    in_schema = False
    for line in lines:
        if line.rstrip(b"\r\n") == b"[schema]":
            in_schema = True
            continue
        if line.startswith(b"["):
            in_schema = False
        if not in_schema:
            output.append(line)
    result = b"".join(output)
    check(b"[window]" in result, "seed snapshot has no window section")
    check(b"[schema]" not in result, "legacy snapshot still has a schema section")
    return result


def write_private(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, mode=0o700, exist_ok=True)
    path.write_bytes(data)
    os.chmod(path, 0o600)


def find_application(pid: int) -> Any:
    import gi

    gi.require_version("Atspi", "2.0")
    from gi.repository import Atspi

    desktop = Atspi.get_desktop(0)

    def candidate() -> Any:
        for index in range(desktop.get_child_count()):
            application = desktop.get_child_at_index(index)
            try:
                if application.get_process_id() == pid and application.get_name() == "ZenPDF":
                    return application
            except Exception:
                continue
        return None

    return wait_until(candidate, "ZenPDF did not appear in the AT-SPI registry")


def descendants(root: Any) -> list[Any]:
    found: list[Any] = []
    pending = [root]
    while pending:
        current = pending.pop()
        found.append(current)
        check(len(found) <= 512, "AT-SPI tree exceeded the inspection bound")
        for index in range(current.get_child_count()):
            pending.append(current.get_child_at_index(index))
    return found


def inspect_failure_dialog(pid: int) -> tuple[dict[str, Any], Any, Any]:
    import gi

    gi.require_version("Atspi", "2.0")
    from gi.repository import Atspi

    application = find_application(pid)
    nodes = descendants(application)
    alerts = [
        node for node in nodes
        if node.get_role_name() == "alert" and node.get_name() == FAILURE_TITLE
    ]
    check(len(alerts) == 1, "preference failure alert role/name is missing or ambiguous")
    alert_nodes = descendants(alerts[0])

    def button(name: str, description: str) -> Any:
        matches = [
            node for node in alert_nodes
            if node.get_role_name() in {"button", "push button"}
            and node.get_name() == name and node.get_description() == description
        ]
        check(len(matches) == 1, f"semantic {name} action is missing or ambiguous")
        action = matches[0].get_action_iface()
        check(action.get_n_actions() >= 1, f"semantic {name} action is not activatable")
        return matches[0]

    cancel = button("Cancel", "Return to ZenPDF")
    discard = button("Discard", "Continue without saving")
    check(
        cancel.get_state_set().contains(Atspi.StateType.IS_DEFAULT),
        "Cancel is not the safe default action",
    )
    check(
        not discard.get_state_set().contains(Atspi.StateType.IS_DEFAULT),
        "Discard unexpectedly became the default action",
    )
    return {
        "role": "alert",
        "name": FAILURE_TITLE,
        "cancel": {
            "role": cancel.get_role_name(),
            "name": cancel.get_name(),
            "description": cancel.get_description(),
            "default": True,
            "actions": cancel.get_action_iface().get_n_actions(),
        },
        "discard": {
            "role": discard.get_role_name(),
            "name": discard.get_name(),
            "description": discard.get_description(),
            "default": discard.get_state_set().contains(Atspi.StateType.IS_DEFAULT),
            "actions": discard.get_action_iface().get_n_actions(),
        },
    }, cancel, discard


def activate(node: Any) -> None:
    action = node.get_action_iface()
    check(action.do_action(0), f"AT-SPI action failed for {node.get_name()}")


def atspi_main_window(pid: int) -> Any:
    matches = [
        node for node in descendants(find_application(pid))
        if node.get_role_name() == "frame" and node.get_name() == MAIN_TITLE
    ]
    check(len(matches) == 1, "main AT-SPI frame is missing or ambiguous")
    return matches[0]


def atspi_action(pid: int, name: str) -> Any:
    matches = [
        node for node in descendants(find_application(pid))
        if node.get_name() == name and node.get_action_iface().get_n_actions() > 0
    ]
    check(len(matches) == 1, f"AT-SPI {name} action is missing or ambiguous")
    return matches[0]


def atspi_window_state(pid: int) -> dict[str, Any]:
    import gi

    gi.require_version("Atspi", "2.0")
    from gi.repository import Atspi

    frame = atspi_main_window(pid)
    extents = frame.get_component_iface().get_extents(Atspi.CoordType.SCREEN)
    states = frame.get_state_set()
    return {
        "x": extents.x,
        "y": extents.y,
        "width": extents.width,
        "height": extents.height,
        "showing": states.contains(Atspi.StateType.SHOWING),
        "visible": states.contains(Atspi.StateType.VISIBLE),
    }


def wait_for_nested_fullscreen(pid: int, message: str) -> dict[str, Any]:
    return wait_until(
        lambda: (
            state
            if (state := atspi_window_state(pid))["width"] == NESTED_SIZE[0]
            and state["height"] == NESTED_SIZE[1]
            and state["showing"]
            and state["visible"]
            else None
        ),
        message,
    )


def enter_nested_presentation(pid: int) -> dict[str, Any]:
    initial = atspi_window_state(pid)
    check(
        (initial["width"], initial["height"]) != NESTED_SIZE,
        "fresh nested launch started fullscreen",
    )
    activate(atspi_action(pid, "Presentation mode"))
    fullscreen = wait_for_nested_fullscreen(pid, "Presentation mode did not fill the nested output")
    return {"initial": initial, "fullscreen": fullscreen}


def quit_nested(pid: int) -> None:
    activate(atspi_action(pid, "Quit"))


def run_normal(
    executable: Path,
    root: Path,
    nested: NestedWayland,
) -> tuple[bytes, dict[str, Any]]:
    root.mkdir(mode=0o700)
    process, capture = launch_nested(executable, root, "first.log", nested)
    presentation = enter_nested_presentation(process.pid)
    quit_nested(process.pid)
    check(wait_for_nested_exit(process, capture) == 0, "normal preference save did not exit cleanly")
    preference = root / "data" / "ZenPDF" / "ZenPDF" / "preferences.ini"
    first_value = metadata(preference)
    check(first_value["regular"] and first_value["effective_uid_owned"], "preference snapshot is unsafe")
    check(first_value["mode"] == "0600" and first_value["links"] == 1, "preference mode/link policy failed")
    first_hash = sha256(preference)

    process, capture = launch_nested(executable, root, "relaunch.log", nested)
    restored = wait_for_nested_fullscreen(
        process.pid,
        "saved fullscreen window state was not restored in nested Wayland",
    )
    check(sha256(preference) == first_hash, "startup rewrote the saved preference snapshot")
    quit_nested(process.pid)
    check(wait_for_nested_exit(process, capture) == 0, "preference relaunch did not exit cleanly")
    final_value = metadata(preference)
    check(final_value["regular"] and final_value["mode"] == "0600", "relaunch snapshot is unsafe")
    return preference.read_bytes(), {
        "distinct_state_saved": "fullscreen",
        "restored_fullscreen": True,
        "nested_initial_extents": presentation["initial"],
        "nested_fullscreen_extents": presentation["fullscreen"],
        "nested_restored_extents": restored,
        "snapshot_unchanged_during_startup": True,
        "preference_after_relaunch": final_value,
        "first_output": bounded_capture(root, "first.log"),
        "relaunch_output": bounded_capture(root, "relaunch.log"),
    }


def run_migration(
    executable: Path,
    root: Path,
    seed: bytes,
    nested: NestedWayland,
) -> dict[str, Any]:
    root.mkdir(mode=0o700)
    legacy = root / "config" / "ZenPDF" / "ZenPDF.conf"
    write_private(legacy, strip_schema(seed))
    legacy_stat = legacy.lstat()
    legacy_hash = sha256(legacy)
    current = root / "data" / "ZenPDF" / "ZenPDF" / "preferences.ini"

    process, capture = launch_nested(executable, root, "first.log", nested)
    wait_for_nested_fullscreen(process.pid, "legacy fullscreen state was not restored on import")
    wait_until(current.exists, "legacy preferences were not imported")
    quit_nested(process.pid)
    check(wait_for_nested_exit(process, capture) == 0, "migration launch did not exit cleanly")

    after = legacy.lstat()
    check(
        (after.st_dev, after.st_ino, after.st_mode, after.st_size) ==
        (legacy_stat.st_dev, legacy_stat.st_ino, legacy_stat.st_mode, legacy_stat.st_size),
        "legacy preference identity or metadata changed",
    )
    check(sha256(legacy) == legacy_hash, "legacy preference bytes changed")
    current_first = metadata(current)
    check(current_first["regular"] and current_first["mode"] == "0600", "migration output is unsafe")

    process, capture = launch_nested(executable, root, "second.log", nested)
    wait_for_nested_fullscreen(process.pid, "migrated fullscreen state was not restored on relaunch")
    quit_nested(process.pid)
    check(wait_for_nested_exit(process, capture) == 0, "relaunch did not exit cleanly")
    current_second = metadata(current)
    check(current_second["regular"] and current_second["mode"] == "0600", "relaunch snapshot is unsafe")
    check(sha256(legacy) == legacy_hash, "relaunch changed the legacy preference bytes")
    return {
        "legacy_immutable": True,
        "fullscreen_restored_on_import_and_relaunch": True,
        "legacy": metadata(legacy),
        "current_after_relaunch": current_second,
        "first_output": bounded_capture(root, "first.log"),
        "second_output": bounded_capture(root, "second.log"),
    }


def run_failure(executable: Path, root: Path) -> dict[str, Any]:
    preference = root / "data" / "ZenPDF" / "ZenPDF" / "preferences.ini"
    preference.mkdir(parents=True, mode=0o700)
    marker = preference / "keep.marker"
    write_private(marker, b"L004 preserve marker\n")
    root.chmod(0o700)
    before_identity = identity(preference)
    before = directory_snapshot(preference)
    process = launch(executable, root, "output.log")
    parent = wait_for_main(process)

    close_client(parent)
    first_dialog = wait_until(
        lambda: client_for(process.pid, FAILURE_TITLE),
        "preference save failure dialog did not map",
    )
    semantics, _, _ = inspect_failure_dialog(process.pid)
    send_escape(first_dialog)
    wait_until(
        lambda: client_for(process.pid, FAILURE_TITLE) is None,
        "keyboard Escape did not cancel the preference failure dialog",
    )
    check(process.poll() is None, "keyboard cancellation unexpectedly exited ZenPDF")
    parent = wait_for_usable_main(process)

    close_client(parent)
    wait_until(
        lambda: client_for(process.pid, FAILURE_TITLE),
        "second preference save failure dialog did not map",
    )
    semantics_second, _, discard = inspect_failure_dialog(process.pid)
    check(semantics_second == semantics, "dialog semantics changed between attempts")
    activate(discard)
    check(wait_for_exit(process) == 0, "AT-SPI Discard did not close ZenPDF cleanly")
    after = directory_snapshot(preference)
    check(identity(preference) == before_identity, "hostile preference directory identity changed")
    check(after == before, "hostile preference directory contents or metadata changed")
    check(not any(item.get("pid") == process.pid for item in clients()), "Wayland client remains")
    return {
        "dialog": semantics,
        "keyboard_escape_cancelled": True,
        "keyboard_escape_returned_to_mapped_app": True,
        "atspi_discard_closed": True,
        "hostile_leaf_identity_unchanged": True,
        "hostile_leaf_inventory_unchanged": True,
        "hostile_leaf_preserved": after,
        "output": bounded_capture(root, "output.log"),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--work-root", required=True)
    parser.add_argument("--expected-sha256", required=True)
    parser.add_argument("--expected-package-version", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    check(os.environ.get("XDG_SESSION_TYPE") == "wayland", "a real Wayland session is required")
    check(bool(os.environ.get("WAYLAND_DISPLAY")), "WAYLAND_DISPLAY is required")
    for program in ("hyprctl", "pacman", "weston"):
        check(shutil.which(program) is not None, f"{program} is required")
    check(BACKGROUND_LAUNCHER.is_file() and os.access(BACKGROUND_LAUNCHER, os.X_OK),
          "codex-background-launch is required")
    executable = Path("/usr/bin/zenpdf")
    check(executable.is_file(), "installed /usr/bin/zenpdf is required")
    check(sha256(executable) == args.expected_sha256, "installed executable hash mismatch")
    installed = command(["pacman", "-Q", "zenpdf-git"]).stdout.decode().strip()
    check(installed == f"zenpdf-git {args.expected_package_version}", "package version mismatch")
    integrity_lines = command(["pacman", "-Qkk", "zenpdf-git"]).stdout.decode().splitlines()
    check(
        integrity_lines and integrity_lines[-1] == "zenpdf-git: 12 total files, 0 altered files",
        "installed package integrity failed",
    )
    check(not zenpdf_processes(), "acceptance requires no pre-existing ZenPDF process")

    work_root = Path(args.work_root)
    check(work_root.is_absolute(), "work root must be absolute")
    work_root.mkdir(mode=0o700)
    check(not any(work_root.iterdir()), "work root must be empty")
    output = Path(args.output)
    check(output.is_absolute(), "output path must be absolute")
    check(output.parent == work_root, "output must be directly under the work root")

    result: dict[str, Any] = {
        "schema": 1,
        "package": {
            "version": args.expected_package_version,
            "executable_sha256": args.expected_sha256,
            "integrity": "12 files, 0 altered",
        },
        "platform": {
            "session": "wayland",
            "hyprland": command(["hyprctl", "version"]).stdout.decode().splitlines()[0],
            "launch": "codex-background-launch on eDP-1/codex-background",
            "nested_wayland": command(["weston", "--version"]).stdout.decode().strip(),
            "omarchy": command(["omarchy", "version"]).stdout.decode().strip(),
        },
    }
    nested: NestedWayland | None = None
    foreground = active_window_identity()
    try:
        nested = NestedWayland(work_root)
        seed, result["normal_save"] = run_normal(executable, work_root / "normal", nested)
        result["migration_relaunch"] = run_migration(
            executable,
            work_root / "migration",
            seed,
            nested,
        )
        nested.close()
        nested = None
        result["save_failure"] = run_failure(executable, work_root / "failure")
        result["no_residual_process"] = not zenpdf_processes()
        check(result["no_residual_process"], "a ZenPDF process remains after acceptance")
        check(active_window_identity() == foreground, "foreground window or workspace changed")
        result["foreground_preserved"] = True
    except BaseException:
        cleanup_active_runs()
        if nested is not None:
            nested.close()
        raise

    payload = (json.dumps(result, indent=2, sort_keys=True) + "\n").encode()
    descriptor = os.open(output, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    try:
        os.write(descriptor, payload)
        os.fsync(descriptor)
    finally:
        os.close(descriptor)
    sys.stdout.buffer.write(payload)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
