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
    if (!preflight.running)
      preflight.running = true
  }

  function close() {}

  Process {
    id: preflight
    command: ["sh", "-c", "command -v zenpdf-launch >/dev/null 2>&1"]

    onExited: (exitCode) => {
      if (exitCode === 0) {
        Quickshell.execDetached(["zenpdf-launch"])
        return
      }

      const message = "The ZenPDF launcher is missing. Reinstall the ZenPDF Arch package and ensure /usr/bin is in PATH."
      console.warn(message)
      Quickshell.execDetached([
        "notify-send",
        "--app-name=ZenPDF",
        "Unable to launch ZenPDF",
        message
      ])
    }
  }
}
