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
    command: ["zenpdf"]
  }
}

