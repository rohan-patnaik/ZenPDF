# L004 persistent local preferences acceptance

L004 is `Verified` for implementation commit
`5426d12462a6ec2bc419dd00b5573b7df9bc609c`, tree
`8e877496f1af50df3fd0db424f97ed7086f7a673`. Its exact Arch package passed
offscreen nested-Wayland save/relaunch, immutable versionless legacy migration,
real Hyprland hostile-leaf preservation, keyboard cancellation, AT-SPI action
semantics, foreground preservation, and clean shutdown. The path-redacted
machine record is
[`acceptance/l004-installed-audit.json`](acceptance/l004-installed-audit.json).

This closes only persistent window preferences. It does not claim document
save/recovery, complete application keyboard/screen-reader operation, package
reproducibility, or the product-wide Omarchy release smoke.

## Immutable identity

| Item | Accepted value |
| --- | --- |
| Implementation commit / tree | `5426d12462a6ec2bc419dd00b5573b7df9bc609c` / `8e877496f1af50df3fd0db424f97ed7086f7a673` |
| Exact-tip CI | [run 32733726003](https://github.com/rohan-patnaik/ZenPDF/actions/runs/32733726003), all five jobs successful |
| Source archive SHA-256 | `738b12ee83fe912ad578695d132099f2692cbc16ee8f87ae9f46e06ef4b913c9` |
| Package / SHA-256 | `zenpdf-git-0.1.0.r0.g5426d12-1-x86_64.pkg.tar.zst` / `ca7a5c8f6e4699b224a374b967deba0053b6fb20783a71ea39c013bfe605873f` |
| Installed package integrity | `zenpdf-git 0.1.0.r0.g5426d12-1`; 12 files checked, 0 altered |
| Installed executable SHA-256 | `b44151524bd7af4b75671f7f05a29f3c8678452ce4b291fe9153b85fb079b4d7` |
| Installed launcher SHA-256 | `5354a8f20b27f088cb44d256d984024ddbac669e97879b30dd5d132d3d8fe21e` |
| Platform | Omarchy `4.0.1-1`; Hyprland `0.56.2`; Weston `15.0.1`; native Wayland |
| Redacted audit SHA-256 | `155ec7786876121b6c793f2816d443bafc79544e18a81e4df68f51f7fbe5dab0` |
| Private screenshot SHA-256 | `893010e937a7f2615d6ef9deccf2b35367d01e3d0a0002bcac3f136b1edc7774` |

The implementation CI jobs were `desktop-arch-package` (`97453153067`),
`product-container-builds` (`97453154101`), `governance` (`97453186221`),
`worker` (`97453195251`), and `web` (`97453203536`); each completed
successfully. The accepted local package also ran all 11 CTest targets during
`makepkg check()`.

## Row-specific results

| L004 criterion | Result |
| --- | --- |
| Private persistence | Pass. In an offscreen 1280x720 Weston compositor, the installed app began as a visible 900x620 AT-SPI frame. Invoking its `Presentation mode` AT-SPI action expanded that frame to 1280x720 and atomically created an effective-user-owned, single-link 0600 schema-1 snapshot. A clean relaunch exposed a visible 1280x720 frame before any harness state change and without rewriting the snapshot during startup. |
| Legacy migration | Pass. A valid versionless QSettings snapshot carrying the saved fullscreen state was imported into the current private leaf and exposed a visible 1280x720 frame on import and relaunch in nested Wayland. The legacy device, inode, mode, size, and SHA-256 remained unchanged across import, close, and relaunch. |
| Failure policy | Pass. Replacing the disposable current preference leaf with a 0700 directory containing a private sentinel produced a bounded path-free failure and required an explicit close decision. The directory device, inode, mode, size, complete immediate inventory, sentinel metadata, and sentinel SHA-256 remained unchanged. |
| Keyboard | Pass. On real Hyprland, targeted Escape canceled the background save-failure dialog and returned to a mapped, visible, input-accepting main client; it did not accept close or change the foreground window/workspace. |
| Real AT-SPI semantics | Pass. The real Hyprland native-Wayland tree exposed one `alert` named `Window preferences not saved`. `Cancel` was the safe default with description `Return to ZenPDF`; `Discard` was non-default with description `Continue without saving`. Both were actionable, and invoking Discard through AT-SPI closed the application cleanly. |
| User-visible workflow | Pass. A private screenshot captured the installed failure dialog on the real compositor. Its raw host/session pixels remain outside the repository; the hash above authenticates it. |
| Shutdown and preservation | Pass. Cancel preserved a usable running app, Discard exited with status 0, no client or ZenPDF process remained, the hostile leaf identity and inventory were unchanged, and every recorded foreground identity field was stable. The harness cleans up both nested and background process groups on all `BaseException` exits. |

No PDF fixture or independent reader is applicable to window preferences. The
normal, versionless-legacy, relaunch, and hostile-local-state cases are the
row-specific inputs. Broader malformed and concurrent snapshots remain covered
by `PreferencesTest.cpp`; this acceptance gate does not duplicate those
source-level fault-injection cases in the installed UI.

## Reproduction

Build and install the exact implementation package from its immutable archive,
then run the acceptance harness from this evidence revision in a real Hyprland
Wayland session. The root README intentionally tracks the current release pin,
so do not substitute that potentially newer revision for this row's accepted
implementation:

```sh
evidence_root=$PWD
zenpdf_revision=5426d12462a6ec2bc419dd00b5573b7df9bc609c
package_root=$(mktemp -d)
git clone --filter=blob:none --no-checkout \
  https://github.com/rohan-patnaik/ZenPDF.git "$package_root/repo"
cd "$package_root/repo"
git fetch --depth=1 origin "$zenpdf_revision"
git checkout --detach "$zenpdf_revision"
test "$(git rev-parse HEAD)" = "$zenpdf_revision"

mkdir "$package_root/source"
git archive --format=tar --prefix=ZenPDF/ "$zenpdf_revision" \
  | tar -xf - -C "$package_root/source"
printf '%s\n' "$zenpdf_revision" \
  >"$package_root/source/ZenPDF/.zenpdf-source-revision"
source_archive="$package_root/repo/apps/desktop/packaging/arch/zenpdf-source-$zenpdf_revision.tar.gz"
tar -C "$package_root/source" -czf "$source_archive" ZenPDF

cd apps/desktop/packaging/arch
package_path=$(ZENPDF_SOURCE_ARCHIVE="$source_archive" \
  ZENPDF_EXPECTED_REVISION="$zenpdf_revision" makepkg --packagelist)
ZENPDF_SOURCE_ARCHIVE="$source_archive" \
ZENPDF_EXPECTED_REVISION="$zenpdf_revision" \
  makepkg --cleanbuild --clean --noconfirm
sudo pacman -U "$package_path"
cd "$evidence_root"

work_root=$(mktemp -d)
rmdir "$work_root"
python3 scripts/acceptance/l004_wayland_acceptance.py \
  --work-root "$work_root" \
  --expected-sha256 \
    b44151524bd7af4b75671f7f05a29f3c8678452ce4b291fe9153b85fb079b4d7 \
  --expected-package-version 0.1.0.r0.g5426d12-1 \
  --output "$work_root/audit.json"
```

The harness requires a clean task-owned root, the exact installed package,
Weston, and `/home/rohan/.local/bin/codex-background-launch`. Normal persistence
and migration run in a headless 1280x720 nested compositor. The hostile-leaf
dialog runs only through the background launcher on `eDP-1`'s hidden
`codex-background` workspace; the harness never focuses or cycles a window and
asserts the foreground identity is unchanged. It bounds captured output,
rejects its case-root name in diagnostics, limits AT-SPI traversal to 512 nodes,
and writes only a path-redacted 0600 audit. Its 900x620 -> 1280x720 -> 1280x720
AT-SPI extent sequence is observable restoration evidence, rather than an
inference from preference bytes alone.
