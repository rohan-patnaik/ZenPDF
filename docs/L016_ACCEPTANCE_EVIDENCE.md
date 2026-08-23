# L016 exact-package acceptance evidence

L016, private recent-files history and purge, is `Verified` for implementation commit
`99b75284775f73ec81d249d003691796a13e92ca` (tree
`555b1b91ff01d803c9c94302fdc88e1ab4ebaa70`). This record reconciles the
criterion-by-criterion results required by
[`LOCAL_STATE_WAYLAND_ACCEPTANCE.md`](LOCAL_STATE_WAYLAND_ACCEPTANCE.md). It is
not evidence for another capability row or for product-wide release readiness.

## Redaction and evidence policy

The retained task bundle contains absolute fixture and state paths, raw logs,
screenshots, and host/session identifiers. They are deliberately not copied
into the repository. This record keeps only product/source hashes, package
identity, software/platform versions, relative evidence names and redacted
results. Fixture contents, passwords, unique path sentinels, user names, user
IDs, inode numbers, display serials, and session addresses are excluded.

The hashes below authenticate the retained records without publishing their
private fields. A future run must use a new non-secret path sentinel and a
task-owned `XDG_DATA_HOME`; it must not reuse these results as evidence for a
different executable.

## Exact identity

| Item | Accepted value |
| --- | --- |
| Source commit / tree | `99b75284775f73ec81d249d003691796a13e92ca` / `555b1b91ff01d803c9c94302fdc88e1ab4ebaa70` |
| Exact-tip CI | [run 32639707196](https://github.com/rohan-patnaik/ZenPDF/actions/runs/32639707196), all five jobs successful |
| Source archive SHA-256 | `ea17a0551615eb3b445c48f0bbfb783ac65ac4f6ad928c1883b1bae3d00572f7` |
| Arch package | `zenpdf-git-0.1.0.r0.g99b7528-1-x86_64.pkg.tar.zst` |
| Package SHA-256 | `9dd3cc9e02d1427e7e1fa565a4f08b3bef3f55afe6b599fa7b940b35daa01bcd` |
| Installed version / integrity | `zenpdf-git 0.1.0.r0.g99b7528-1`; 12 files checked, 0 altered |
| Installed `/usr/bin/zenpdf` SHA-256 | `c9db39c18fb08c820a6469ad0f1a60499b4a1653c0dcec66673f6d8c082e32ee` |
| Rollback package SHA-256 | `d231fc58f62847fbf2603b9b29dcf54334457d83b47a3a0cc2ba88b67b6402e2` |
| Native platform | Omarchy `4.0.0-1`; Wayland; Hyprland `0.56.2`; display scale `1` |

Package-to-installed reconciliation byte-matched the executable, launcher,
desktop entry, and metainfo. The clean source checkout remained unchanged.

The approved fixture chain of custody is specific rather than inferred. The
pre-acceptance fixture manifest created during the pinned build recorded
SHA-256 `8786281deca6644b1b30f5c73a21ab6338c89d8976c3739847e6b9f13c0132e2`.
The direct run's live identity recorded the same hash. The launcher recorded
explicit before and after hashes with that value. Its after record immediately
preceded the desktop run, whose running command line identified the same
fixture. Finally, the path-redacted
[`l016-postshutdown-audit.json`](acceptance/l016-postshutdown-audit.json),
generated after all three processes exited, recorded the same fixture hash.
There is no separately named immediate after-file for the direct or desktop
run; this ordered record is the durable preservation claim.

## Criterion reconciliation

| Acceptance criterion | Result and evidence |
| --- | --- |
| Identity, CI, package and rollback | Pass. The exact values above were recorded before acceptance; the rollback package was reinstalled and integrity-checked before the candidate was installed. |
| Disposable state and source preservation | Pass. Every run used a task-owned isolated XDG state root; the ordered fixture chain above ends in a post-shutdown hash match. No user's real state was modified. |
| Direct `/usr/bin/zenpdf`, umask 022 | Pass on real Wayland. The live record showed a `0700` leaf and regular, single-link, effective-user-owned `0600` database/WAL/SHM. The later post-shutdown audit confirmed the leaf and remaining database; WAL/SHM were absent after close. |
| Desktop/MIME entry | Pass on real Wayland. The running executable resolved to `/usr/bin/zenpdf`; live private-state and purge checks passed. The later post-shutdown audit confirmed the leaf and remaining database; WAL/SHM were absent after close. |
| Installed `zenpdf-launch` | Pass on real Wayland. The launcher payload matched the package; live private-state and purge checks passed. The later post-shutdown audit confirmed the leaf and remaining database; WAL/SHM were absent after close. |
| Recent path locality | Pass for all three entry paths. The approved sentinel appeared in the private SQLite WAL while live and was absent from product diagnostics. |
| UI purge | Pass for all three entry paths. Each UI-driven **Clear Recent** record found no sentinel in the live database/WAL/SHM after the purge sequence. The later post-shutdown audit found no sentinel in any remaining database or diagnostics; WAL/SHM were absent. |
| Leaf symlink | Pass on real Wayland. Exit `1`; target unchanged; no disclosure or residual process. |
| Database symlink | Pass with the exact installed binary under Qt offscreen. Exit `1`; link and target hash unchanged; no disclosure or residual process. |
| Wrong-owner database | Pass on real Wayland using normal filesystem ownership controls. Exit `1`; object unchanged; no WAL/SHM, disclosure or residual process. |
| FIFO/non-regular database | Pass on real Wayland. Prompt exit `1`; FIFO unchanged; no WAL/SHM, disclosure or residual process. |
| Multi-link database | Pass on real Wayland. Exit `1`; database and peer unchanged; no WAL/SHM, disclosure or residual process. |
| `0777` application leaf | Pass with the exact installed binary under Qt offscreen. Exit `1`; leaf mode and zero-entry state unchanged; no disclosure or residual process. |
| Separate `0666` database, WAL and SHM | Pass in three independent exact-binary Qt-offscreen runs. Each exited `1`; the unsafe empty file hash and metadata stayed unchanged; no disclosure or residual process. |
| Error modal and shutdown | Pass. Real-Wayland hostile cases exposed an active AT-SPI alert named `ZenPDF could not start`; message bodies were bounded and redacted. Clean runs exited without a residual process; hostile runs exited `1` without hangs or crashes. |
| Independent acceptance | Pass. Independent High source review had no remaining findings, and independent High installed acceptance accepted this bounded L016 slice. |

The five platform-independent follow-up cases (database symlink, `0777` leaf,
and separate `0666` database/WAL/SHM) used `QT_QPA_PLATFORM=offscreen` because
the managed follow-up sandbox returned `EPERM` for the existing host Wayland
and D-Bus sockets. They used the same installed executable hash shown above.
The exact [`l016_autodismiss.cpp`](../scripts/acceptance/l016_autodismiss.cpp)
and [`l016_autodismiss.map`](../scripts/acceptance/l016_autodismiss.map)
interposer sources are retained in-repository. The harness replaced only
`QMessageBox::critical`, recorded bounded redacted properties, and returned OK
so the failure process could terminate:

```text
l016-autodismiss.cpp  sha256=e7bdf18be1a49d49886c7943d85307d9eb3d0ff75c492482ca759bd3ce755058
l016-autodismiss.map  sha256=dea950a1e5fa03684709921f62dee767b2f152698dd287e7c546bf2d23e12bb6
l016-autodismiss.so   sha256=28785ee41798ff022246d7829eac86844ee670abd732f4cc734fc44e7acf0c7c
```

This disclosure limits those five results to platform-independent local-state
failure behavior. The native UI, entry-path, purge and AT-SPI requirements are
instead satisfied by the real-Wayland runs of the same binary.

## Reproduction template

Run each entry path and hostile case serially. Substitute only task-owned paths:

```sh
set -eu
candidate=/usr/bin/zenpdf
expected=c9db39c18fb08c820a6469ad0f1a60499b4a1653c0dcec66673f6d8c082e32ee
test "$(sha256sum "$candidate" | cut -d' ' -f1)" = "$expected"
exec 9>/tmp/zenpdf-package-ui-acceptance.lock
flock -n 9                         # external acceptance serialization only

case_root=$(mktemp -d)
chmod 700 "$case_root"
export XDG_DATA_HOME="$case_root/data"
export XDG_CONFIG_HOME="$case_root/config"
export XDG_CACHE_HOME="$case_root/cache"
sentinel="L016_REPRO_$(date +%s)_$$.pdf"
fixture="$case_root/$sentinel"       # create an approved, non-secret PDF here
fixture_before=$(sha256sum "$fixture")

# Native entry paths, one per fresh case_root:
umask 022; /usr/bin/zenpdf "$fixture"
gtk-launch io.github.rohan-patnaik.zenpdf "$fixture"
zenpdf-launch "$fixture"

# While live and after shutdown, use lstat/stat to require leaf 0700 and each
# existing SQLite object regular, one-link, effective-user-owned and 0600.
# Binary-scan state.sqlite3{,-wal,-shm} and capped diagnostics before purge;
# invoke File > Recent Files > Clear Recent; wait for clean exit; scan again.
# Require the sentinel present only in SQLite before purge and absent afterward.

test "$(sha256sum "$fixture")" = "$fixture_before"
! pgrep -x zenpdf
```

For hostile cases, create exactly one condition in a fresh root: symlink the
leaf; symlink, `chown`, `mkfifo`, or hard-link `state.sqlite3`; `chmod 0777` the
leaf; or `chmod 0666` exactly one of the database/WAL/SHM files. Capture
`lstat`, link count, owner, mode, size and SHA-256 where applicable before and
after. Run the candidate with a bounded timeout and capped output. Require exit
`1`, unchanged applicable metadata/hash/target, no new sidecar, no raw path,
basename or SQLite/driver text, and no residual process. The `/tmp` lock above
belongs to the external acceptance workflow, not ZenPDF. Ownership cases must
use normal filesystem ownership controls.

Compile and run the offscreen-only modal interposer as follows. The accepted
shared-object hash below is toolchain-specific; verify the retained source/map
hashes before compiling and record the new shared-object hash on a rerun.

```sh
c++ -std=c++23 -O2 -fPIC -shared \
  $(pkg-config --cflags Qt6Widgets) \
  scripts/acceptance/l016_autodismiss.cpp \
  -Wl,--version-script=scripts/acceptance/l016_autodismiss.map \
  $(pkg-config --libs Qt6Widgets) \
  -o "$case_root/l016-autodismiss.so"

set +e
env QT_QPA_PLATFORM=offscreen QT_QPA_PLATFORMTHEME= QT_STYLE_OVERRIDE=Fusion \
  LD_PRELOAD="$case_root/l016-autodismiss.so" \
  timeout 10s /usr/bin/zenpdf >"$case_root/capped.log" 2>&1
status=$?
set -e
test "$status" -eq 1
test "$(wc -c <"$case_root/capped.log")" -le 4096
test "$(grep -Ec '^L016_AUTODISMISS title_ok=yes message_bytes=[0-9]+ bounded=yes path_free=yes basename_free=yes driver_free=yes$' "$case_root/capped.log")" -eq 1
```

## Retained-record hashes

The first four hashes identify the redacted record and exact acceptance helper
sources committed here. The remaining relative names identify records in the
private acceptance bundle; those files remain outside the repository because
they contain private paths or host details.

```text
dde6a9360660622c1a1f34df1d83271f92922c83f1cc2f2c2265d4a0db085aa0  docs/acceptance/l016-postshutdown-audit.json
0b4e8f0bd7980b9425b3c4a74a412753168b9f2fea695dcdfa69e6c0c96d0b45  scripts/acceptance/l016_postshutdown_audit.py
e7bdf18be1a49d49886c7943d85307d9eb3d0ff75c492482ca759bd3ce755058  scripts/acceptance/l016_autodismiss.cpp
dea950a1e5fa03684709921f62dee767b2f152698dd287e7c546bf2d23e12bb6  scripts/acceptance/l016_autodismiss.map
30cbed631369b4f9c8eade2c3cb0ac468d44ff78ca470f94266c1b0e4b32abeb  reconcile/source-identity.txt
c4b410cc41772f946ca5bd750db7c9c568a2cdd5446ffaf58aa074bf06e7c4f4  reconcile/exact-tip-ci.json
e97c830a8bebf68131a8a247a445def3c53e831dffa46935a33823c481fcd1ff  reconcile/installed-payload-match.txt
93def891550b48d04ca74695d93003d13cdb9e09f121a75559bc14dabed03d60  ui-direct/privacy-present.txt
a0a22abef58077f4100460f49e3bd0616a6673d94235148770d73261997df48c  ui-direct/privacy-purged.txt
0b25eba5b670d90ace0ba477021b276d5f1803875202dd2d8fb51da55471a22c  ui-desktop/privacy-present.txt
355ddf7e57a572bc16880bd786c73046082a053807f25cbbca5bea8060ad1ee8  ui-desktop/privacy-purged.txt
457b1c31d7332a0a4a11f0aa9ab4a000c2b64c93e7ee7cb12e0cac67e3ba17e3  ui-launcher/privacy-present.txt
8471f82804fac974b9370b4150a8587e6f4c07ec6f437959078934a7eade615a  ui-launcher/search-privacy-purged-verified-2.txt
6dc766beea794dc66627ea5753ca803aad23a6dd61b8f4fbd232200bba0958d7  hostile/symlink-directory/final-verification.txt
930b1f02e0baa850903ecfeba9c312d7c80fffe847f8df4e0db91676aacc9ccf  hostile/wrong-owner-database/final-verification.txt
36da49ecc2e046a03f41788bbea785d2242579b7f7ad13ccacc964e9702fef07  hostile/fifo-database/final-verification.txt
4eefdc54236fba5f82a00067db699f0b6012a1721c6110db4212f78f2bb633cf  hostile/multilink-database/final-verification.txt
```

The hash-complete offscreen follow-up's before/after record hashes were
identical for every applicable object: database symlink
`4243acbb683d412b7d6a674a4b655b66ac3cc57061c6d183000eeb71b2406461`,
`0777` leaf
`a2afff3f30a69653f6e2dee841e23c5e0867d7622607306b6de3d53317a25352`,
`0666` database
`8a550667beabfa70fb13c208bc8b80fa9fdeff7830a1b68c26b6afd5f958a4f3`,
`0666` WAL
`52490d6e73e3e8f34c15756ee3af4c11d51e91aa620fd72e760981a4406d8af4`,
and `0666` SHM
`3b8f1b2cf924e37f379a9de39dad7a42d4e5530c479f063b8fd1d06f8272e750`.
The symlink target stayed at SHA-256
`5eb733502347eb6b6f9f7529f3827b1418ba9fbd46fbaf7637b574a137d4757b`;
the unsafe database/WAL/SHM fixtures stayed empty at SHA-256
`e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855`;
and the `0777` leaf stayed at zero entries.
The corresponding redacted result-record hashes were, in the same order,
`dfd1729f9249ad13c2d4eeeeac0d5105e613f80d1ebbace0e0bb7bb8e26ed851`,
`9f03958e2f4a025fd5bbdb1b3a5212abe33f81ff4ac7edef7688ccd73826542a`,
`a6b317fbb37f803b1d660f3c2d07a3497777b59ad56089236d7dba0cb538ba23`,
`d72fe9db65d0785de882464d0dbe899534c584f4f72d6af0decbe86e3e7d6b9b`,
and `1947a814e7a0ec7d6bbf6600cd8abe1b94d66f0ffffe1ad6eab8175f2d79969a`.
