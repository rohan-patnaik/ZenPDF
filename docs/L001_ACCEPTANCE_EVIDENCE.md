# L001 native Arch/Wayland launch evidence

L001, native Arch/Wayland application launch, is `Verified` for implementation
commit `99b75284775f73ec81d249d003691796a13e92ca` (tree
`555b1b91ff01d803c9c94302fdc88e1ab4ebaa70`). Its exact Arch package visibly
mapped `/usr/bin/zenpdf` as a native Wayland client on Omarchy and shut down
without a residual process. This record reconciles that row-specific evidence;
it is not a claim for the separate Quickshell, desktop-environment, package
reproducibility, accessibility, or release-smoke rows.

## Redaction policy

The retained acceptance bundle includes absolute fixture/state paths, process
IDs, compositor addresses, display identifiers, screenshots, and raw logs.
They are not copied into the repository. This record retains only source and
package identity, software/platform versions, redacted launch properties,
relative evidence names, and SHA-256 hashes that authenticate the private
records. A rerun must use a fresh task-owned state root and record new evidence;
these results must not be reused for another executable.

## Identity and chronology

| Item | Accepted value |
| --- | --- |
| Installed implementation commit / tree | `99b75284775f73ec81d249d003691796a13e92ca` / `555b1b91ff01d803c9c94302fdc88e1ab4ebaa70` |
| Implementation exact-tip CI | [run 32639707196](https://github.com/rohan-patnaik/ZenPDF/actions/runs/32639707196), all five jobs successful |
| Source archive SHA-256 | `ea17a0551615eb3b445c48f0bbfb783ac65ac4f6ad928c1883b1bae3d00572f7` |
| Package / SHA-256 | `zenpdf-git-0.1.0.r0.g99b7528-1-x86_64.pkg.tar.zst` / `9dd3cc9e02d1427e7e1fa565a4f08b3bef3f55afe6b599fa7b940b35daa01bcd` |
| Installed version / integrity | `zenpdf-git 0.1.0.r0.g99b7528-1`; 12 files checked, 0 altered |
| Installed executable SHA-256 | `c9db39c18fb08c820a6469ad0f1a60499b4a1653c0dcec66673f6d8c082e32ee` |
| Installed launcher SHA-256 | `5354a8f20b27f088cb44d256d984024ddbac669e97879b30dd5d132d3d8fe21e` |
| Platform | Omarchy `4.0.0-1`; Wayland; Hyprland `0.56.2`; display scale `1` |
| Published branch | `feat/omarchy-desktop-m0-m1` |
| Published evidence parent / tree | `de08d95d94e2567cb901e357112b9c84cc248bf2` / `5cc8396b26953ce395ff61769cb0bd974c7cd98d` |
| Evidence-parent exact-tip CI | [run 32645388345](https://github.com/rohan-patnaik/ZenPDF/actions/runs/32645388345), all five jobs successful |

The evidence-parent CI jobs were `product-container-builds` (`97208755242`),
`desktop-arch-package` (`97208755301`), `worker` (`97208755345`), `web`
(`97208755363`), and `governance` (`97208755367`); each completed successfully.

The package-to-installed reconciliation byte-matched the executable, launcher,
desktop entry, and metainfo. Between the installed implementation and the
evidence parent, Git records changes only under `docs/`, `scripts/acceptance/`,
and the governance test; no desktop source, launcher, manifest, plugin, or
packaging file changed. The later evidence commits therefore reconcile the
already accepted implementation/package rather than claiming that a
documentation-only descendant package was installed.

The evidence parent's live GitHub commit, tree, and CI result were authenticated
by the controlling root connector. This editor sandbox could not resolve
GitHub, so its clean task-owned clone was created from the HTTPS origin-tracking
object whose reflog records an update by push, then retained the canonical HTTPS
origin. This limitation affects only redundant network retrieval, not the
authenticated identities above.

## Row-specific results

| L001 criterion | Result |
| --- | --- |
| Native Arch package | Pass. The exact source archive produced the package above; package install and `pacman -Qkk` reconciliation passed. |
| Installed payload identity | Pass. `/usr/bin/zenpdf` and `/usr/bin/zenpdf-launch` byte-matched their package members. |
| Omarchy Wayland session | Pass. Acceptance ran on Omarchy `4.0.0-1`, `XDG_SESSION_TYPE=wayland`, Hyprland `0.56.2`, scale `1`. |
| Visible native client | Pass. Starting the installed `zenpdf-launch` payload produced one mapped, visible, input-accepting client titled `ZenPDF`, class `io.github.rohan-patnaik.zenpdf`, with `xwayland=false`. The recorded PID joined the running `/usr/bin/zenpdf` process to that client; a redacted AT-SPI tree and screenshot independently showed the visible ZenPDF application/frame and local-workspace UI. |
| Native executable | Pass. The desktop entry separately resolved its running executable to `/usr/bin/zenpdf`; direct execution also reached the ZenPDF UI under umask `022`. |
| Launch diagnostics | Pass for this successful-launch criterion. Launcher output was bounded; observed portal/AT-SPI warnings did not terminate launch or disclose document content. Failure-path detail remains part of L007, not L001. |
| Shutdown | Pass. The accepted client exited cleanly, left no ZenPDF process, and did not hang. |
| CI/build regression | Pass. Implementation CI configured and built the native application, ran complete CTest, and built the exact-revision Arch package; the published evidence parent also passed all five exact-tip jobs. |

No independent document-reader interoperability applies to an application
launch. L001 does not require the root `Plugin.qml` to be invoked from a live
Quickshell process; that is L007. It also does not require portal/theme matrices
(L003), install/remove or independent reproducibility (L008/L074), complete
assistive-technology operation (L039), or product-wide Quattro release smoke
(L076).

## Reproduction template

Run in an actual Omarchy Wayland session with no ZenPDF process present. Keep
the full command output and unredacted compositor record private; publish only
the bounded fields above.

```sh
set -eu
exec 9>/tmp/zenpdf-package-ui-acceptance.lock
flock -n 9                         # external acceptance serialization only

test "${XDG_SESSION_TYPE:-}" = wayland
test -n "${WAYLAND_DISPLAY:-}"
test "$(omarchy-version)" = 4.0.0-1
case "$(hyprctl version | sed -n '1p')" in
  'Hyprland 0.56.2 '*) ;;
  *) exit 1 ;;
esac
hyprctl -j monitors | jq -e 'any(.[]; .focused == true and .scale == 1)' \
  >/dev/null
test "$(sha256sum /usr/bin/zenpdf | cut -d' ' -f1)" = \
  c9db39c18fb08c820a6469ad0f1a60499b4a1653c0dcec66673f6d8c082e32ee
test "$(sha256sum /usr/bin/zenpdf-launch | cut -d' ' -f1)" = \
  5354a8f20b27f088cb44d256d984024ddbac669e97879b30dd5d132d3d8fe21e
test "$(pacman -Q zenpdf-git)" = 'zenpdf-git 0.1.0.r0.g99b7528-1'
integrity=$(pacman -Qkk zenpdf-git 2>&1) || {
  printf '%s\n' "$integrity" >&2
  exit 1
}
test "$(printf '%s\n' "$integrity" | tail -n 1)" = \
  'zenpdf-git: 12 total files, 0 altered files'
! pgrep -x zenpdf

run_root=$(mktemp -d)
chmod 700 "$run_root"
export XDG_DATA_HOME="$run_root/data"
export XDG_CONFIG_HOME="$run_root/config"
export XDG_CACHE_HOME="$run_root/cache"
export XDG_STATE_HOME="$run_root/state"
zenpdf-launch

attempt=0
while [ "$attempt" -lt 50 ]; do
  app_pid=$(pgrep -nx zenpdf || true)
  if [ -n "$app_pid" ] &&
    [ "$(readlink -f "/proc/$app_pid/exe")" = /usr/bin/zenpdf ] &&
    hyprctl -j clients | jq -e --argjson pid "$app_pid" '
    any(.[];
      .pid == $pid and .mapped == true and .hidden == false and
      .visible == true and .acceptsInput == true and
      .xwayland == false and .class == "io.github.rohan-patnaik.zenpdf" and
      .title == "ZenPDF")
  ' >/dev/null; then
    break
  fi
  attempt=$((attempt + 1))
  sleep 0.1
done
test "$attempt" -lt 50

# Close ZenPDF through File > Quit, wait for exit, then require:
! pgrep -x zenpdf
```

The `/tmp` lock belongs to the external acceptance workflow, not ZenPDF.

## Retained-record hashes

These names are relative to the private installed-acceptance bundle. Hashes
authenticate records that remain outside the repository because their raw
forms contain private paths or host/session identifiers.

```text
17a48ecc67b7befc564beab1b1e5ca4a4404f095a86f5958b48905fbe7beaaae  preflight/readonly-probe.txt
30cbed631369b4f9c8eade2c3cb0ac468d44ff78ca470f94266c1b0e4b32abeb  reconcile/source-identity.txt
c4b410cc41772f946ca5bd750db7c9c568a2cdd5446ffaf58aa074bf06e7c4f4  reconcile/exact-tip-ci.json
fc72888788f04f502ade0701abd8867835a398922793957317f660abf9b0e2c2  reconcile/installed-integrity.txt
e97c830a8bebf68131a8a247a445def3c53e831dffa46935a33823c481fcd1ff  reconcile/installed-payload-match.txt
62c8f1a1bb5d7b47a55cf07ef0de163cfe301a4ce923bf00c24feb2aae93f75c  ui-launcher/installed-package.txt
73f864e841c80ffc7304d0b97c5cc8f80a4041ec3a7a6e908b7411ca8d33072c  ui-launcher/installed-payload.sha256
035bf1955b25583ed937347496b575565c28cb33190142fd64abec899cede44f  ui-launcher/zenpdf.pid
0e5bfe22bc29345f7b945d246184c320ce284fa526af40cabb18ef6202c1edcc  ui-launcher/thumbnail-rss-initial.txt
2e0ab2493f86896dec9cee9a1d5bd22f22fe667ca1ccb7ce9c3b125eb1b4c03c  ui-launcher/wayland-client.json
6e815a64be9f064991c7a4078a173b66b36c46bc1e2512a11a4b44d49b800c5c  ui-launcher/wayland-client-full.json
1f89b384130e129cf7738e8f2d5e882bc2cc53892248e62fca9a379a9ddaf72b  ui-launcher/launcher-empty.png
6e70c57fa919141b658f770ecdb7dc67f8ac3575f9dabb5a3c6120ebadb62516  ui-launcher/atspi-empty.json
97428dcb6cde28e6e2f3c8718383c62b54547e1b6a22c69481abc7848bdff3c5  ui-launcher/shutdown-verification.txt
f773689cf8260f6f3cbe23f119f253f4beeeb2e18028481a392cbeb13403f2b4  ui-desktop/running-exe.txt
1f2c91e1c0fe93c0478eb28f4807052b53972a15f2ae109712bb6ec3b4a24c99  ui-desktop/shutdown-verification.txt
1f71ef35907a4c4c8598e7346636fb9ffcb1c522be8d46656144893c02d63d83  ui-direct/direct-identity.txt
92772341ccde0b239e8648912589be01ed570ceaea47f7e1fdc6555296b60cfe  ui-direct/direct-stdout-stderr.log
799a8b1af312b4f52d28937470477ae0616bbbb40345ca352b09c22e4e5085f3  FINAL_ACCEPTANCE.md
```
