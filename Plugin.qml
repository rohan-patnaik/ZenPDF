import QtQuick
import Quickshell
import Quickshell.Io

Item {
  id: root

  property string omarchyPath: ""
  property var shell
  property var manifest
  property var pluginRegistry

  function open(payloadJson) {
    if (!launcher.running)
      launcher.running = true
  }

  function close() {}

  Process {
    id: launcher
    command: [
      "sh",
      "-lc",
      "if command -v zenpdf >/dev/null 2>&1; then exec zenpdf; fi; "
        + "message='ZenPDF is not installed or is not available in PATH.'; "
        + "if command -v notify-send >/dev/null 2>&1; then "
        + "notify-send --app-name=ZenPDF 'Unable to launch ZenPDF' \"$message\"; fi; "
        + "printf '%s\\n' \"$message\" >&2; exit 127"
    ]
  }
}

