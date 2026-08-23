# L005 private bounded diagnostics acceptance

L005 is `Verified` for implementation commit `aa118d8a9544e4982ef8110988f95e45fa9ccf3b`, tree `649c6283b40f22b672f3f4f436ddab03b9809051`, parent `f2fac2351658c677afc4519e4b44dcf0fad30592`. The implementation, focused tests, exact-source Release package, task-owned installed runtime, privacy forensics, crash/restart behavior, and independent High review and acceptance all passed. The path-free machine-readable summary is [`acceptance/l005-installed-audit.json`](acceptance/l005-installed-audit.json).

This closes only L005. The accepted runtime used Qt offscreen inside a user namespace with the task-owned package payload mapped to `/usr/bin/zenpdf` and host Qt/qpdf libraries. The mapping was host-backed and the runtime executable hash equaled both the package member and task-owned pacman-root payload. There was no exact-`aa118d8` real-Wayland run, so this record adds no L001 or L076 claim.

## Criterion reconciliation

| L005 criterion | Durable result |
| --- | --- |
| Private local creation | New log leaves were effective-user-owned mode 0700; log children were single-link regular files at mode 0600. Package and namespace identity checks passed. |
| Two-file hard bound | Active and rotated files stayed at or below 1 MiB, including exact-limit, active-oversize, rotated-oversize, and both-oversize starts. Requests above the product ceiling did not raise it. |
| Hostile filesystem state | Six directory variants and sixteen active/rotated child variants covered file, directory, FIFO, symlink, hardlink, wrong owner, 0400, 0644, 0666, repairable 0755, and unsafe 0777 states. Unsafe objects and external targets were unchanged; the allowed directory repair produced 0700 plus private logging. |
| Content privacy | The approved sensitive body, path/basename markers, category, source symbol, control sequence, and display diagnostics were absent from stderr and both logs. Only `application-start` and `message-suppressed` product events matched the bounded grammar. Setup failure and full application teardown retained the sanitizer; fixture bytes were unchanged. |
| Multi-instance rotation | Eight simultaneous installed processes emitted 1,000 events each into a 900 KiB valid filler state. The two visible files retained exactly eight starts and 22,629 suppressed events, with no open-but-unlinked log descriptor while processes waited. |
| Bounded contention | Setup completed under the external lock in 252.740 ms and recovered in the same process. Runtime contention skipped the file event in 111.047 ms, preserved state, and recovered after release. No case hung. |
| Fatal and restart | Fatal contention produced SIGABRT in 214.096 ms after trigger, no stored core, no deleted descriptor, no sensitive output, and a clean same-state restart. Exactly one list candidate and one unique marker match joined to the accepted process identity; its precise list time was 168.550 ms after trigger and 45.596 ms before process end. `coredumpctl info` `Timestamp` was rounded/display-only. |
| Size-inspection failure | The accepted `fstat` fault interposer left an exact 1 MiB active file byte-for-byte unchanged, emitted only sanitized stderr, and allowed normal rotation/recovery after the fault was removed. |
| Package/source integrity | Clean source SHA/tree, revision marker, source archive, package, package member, installed payload, pacman query, and 12-files/0-altered result agreed. Rollback and candidate reinstall each passed query and integrity checks; the host production package was untouched. |
| Review and CI | Independent High review and High acceptance signed off all 34 installed cases. Exact-tip CI run [32651427740](https://github.com/rohan-patnaik/ZenPDF/actions/runs/32651427740) completed successfully at `aa118d8`; desktop Arch package (`97223541667`), web (`97223541694`), product-container builds (`97223541714`), governance (`97223541734`), and worker (`97223541743`) were green. |

## Immutable identities

- Local exact-source Release package: `0767a68df9aead7f9ec5bfeb595b8dcc0d7ba4ed93b48c5925d1ebc2ed2dac99`; version `0.1.0.r0.gaa118d8-1`.
- Source archive: `c1a5c8699647ad08865fccbe05efe13d8cad72d404bdc2764cb1bb8ebe8f17f8`.
- Candidate package member and installed executable: `a48f20268555796a7f159508a27cd6a36c2561a936cfb7767c5a4ea072292416`.
- Rollback package: `d231fc58f62847fbf2603b9b29dcf54334457d83b47a3a0cc2ba88b67b6402e2`; rollback executable `7575cdbf2e7fa30f429039acc7631cf577c71d0cfbad9150411fad42bb8e206c`.
- Untouched host production package: `0.1.0.r0.g99b7528-1`; executable `c9db39c18fb08c820a6469ad0f1a60499b4a1653c0dcec66673f6d8c082e32ee`.
- Private raw acceptance record: `2839344f58c9be843d49d800b5b007e65d67780983acc9c34e665591c00110a3`.
- Reproducible private bundle manifest: `34d0c9c2f862a1a63bd0a5d1d8dc4843e0e06d6b5a8611c89ffcb906ab6ed7a2`.
- Candidate and rollback package-member manifests: `c37eb2b25500f7d2657a8e70375353dd3fd592fd497ff2513e3e0d21e333ef4b` and `8ee31a842e628b6d2c8138e9f67151e03308a7b97d7a42b00980436b0c86e9c2`.
- Final evidence index: `c348ac2984fb77fcdda43d87398d1b640b052a3654880a1ab9aa6dc669cffef4`.

The retained source archive and accepted package were authenticated by their hashes above. CI run 32651427740 independently built and tested the same source SHA but uploaded no artifact; its green package job is not represented as the accepted package's provenance. A fresh archive/build can reproduce the source tree, revision marker, package version, installed member set, and executable payload, but not the accepted full archive/package hashes unless tar/gzip and makepkg metadata are normalized. In particular, `.BUILDINFO`/`.PKGINFO` build date and build directory metadata vary. L074 therefore remains `Partial`.

## Reproduction

The accepted host used Git 2.55.0, CMake 4.4.2, Ninja 1.13.2, GCC/G++ 16.2.1, Python 3.14.7, makepkg/pacman 7.1.0, bsdtar 3.8.9, GNU readelf 2.47, Qt 6.11.2, and qpdf 12.4.0. Runtime package versions were `qt6-base 6.11.2-2`, `qt6-webengine 6.11.2-1`, `qpdf 12.4.0-1`, `glibc 2.44+r24+g16be1518495f-1`, and `bubblewrap 0.11.2-1`.

Use three separate lanes: `evidence_repo` at the reviewed evidence commit, a detached clean `implementation_repo` at the accepted implementation, and `package_work` outside both. Replace the evidence placeholder only after its review:

```sh
sha=aa118d8a9544e4982ef8110988f95e45fa9ccf3b
tree=649c6283b40f22b672f3f4f436ddab03b9809051
reviewed_evidence_sha=REPLACE_WITH_REVIEWED_EVIDENCE_40_CHARACTER_SHA
work_root=$(mktemp -d)
git clone --branch feat/omarchy-desktop-m0-m1 --single-branch \
  https://github.com/rohan-patnaik/ZenPDF "$work_root/evidence_repo"
git -C "$work_root/evidence_repo" checkout --detach "$reviewed_evidence_sha"
test "$(git -C "$work_root/evidence_repo" rev-parse HEAD)" = "$reviewed_evidence_sha"
test -z "$(git -C "$work_root/evidence_repo" status --porcelain=v1)"
git clone --no-checkout https://github.com/rohan-patnaik/ZenPDF \
  "$work_root/implementation_repo"
git -C "$work_root/implementation_repo" checkout --detach "$sha"
test "$(git -C "$work_root/implementation_repo" rev-parse HEAD)" = "$sha"
test "$(git -C "$work_root/implementation_repo" rev-parse HEAD^{tree})" = "$tree"
test -z "$(git -C "$work_root/implementation_repo" status --porcelain=v1)"
mkdir "$work_root/package_work"
evidence_repo="$work_root/evidence_repo"
implementation_repo="$work_root/implementation_repo"
package_work="$work_root/package_work"
```

Authenticate the retained accepted artifacts. These hashes identify the frozen evidence; they are not assertions about a fresh build:

```sh
accepted_source_archive=/path/to/retained/zenpdf-source-aa118d8.tar.gz
accepted_package=/path/to/retained/zenpdf-git-0.1.0.r0.gaa118d8-1-x86_64.pkg.tar.zst
rollback_package=/path/to/retained/zenpdf-git-0.1.0.r0.geeba33d-1-x86_64.pkg.tar.zst
test "$(sha256sum "$accepted_source_archive" | cut -d' ' -f1)" = \
  c1a5c8699647ad08865fccbe05efe13d8cad72d404bdc2764cb1bb8ebe8f17f8
test "$(sha256sum "$accepted_package" | cut -d' ' -f1)" = \
  0767a68df9aead7f9ec5bfeb595b8dcc0d7ba4ed93b48c5925d1ebc2ed2dac99
test "$(sha256sum "$rollback_package" | cut -d' ' -f1)" = \
  d231fc58f62847fbf2603b9b29dcf54334457d83b47a3a0cc2ba88b67b6402e2
```

Create a fresh tree-equivalent source archive, verify its marker/content, and make a Release package in `package_work`. Full archive/package hashes can vary because tar/gzip and makepkg metadata are not normalized; require the semantic source and executable payload instead:

```sh
mkdir -p "$package_work/source/ZenPDF" "$package_work/expected" \
  "$package_work/unpacked" "$package_work/arch"
git -C "$implementation_repo" archive "$sha" | \
  tar -x -C "$package_work/source/ZenPDF"
printf '%s\n' "$sha" >"$package_work/source/ZenPDF/.zenpdf-source-revision"
tar -C "$package_work/source" -czf "$package_work/arch/zenpdf-source-$sha.tar.gz" ZenPDF
git -C "$implementation_repo" archive "$sha" | tar -x -C "$package_work/expected"
tar -xzf "$package_work/arch/zenpdf-source-$sha.tar.gz" -C "$package_work/unpacked"
test "$(cat "$package_work/unpacked/ZenPDF/.zenpdf-source-revision")" = "$sha"
diff -qr "$package_work/expected" "$package_work/unpacked/ZenPDF" \
  --exclude=.zenpdf-source-revision
cp -a "$implementation_repo/apps/desktop/packaging/arch/." "$package_work/arch/"
cd "$package_work/arch"
ZENPDF_SOURCE_ARCHIVE="zenpdf-source-$sha.tar.gz" \
ZENPDF_EXPECTED_REVISION="$sha" \
QT_QPA_PLATFORM=offscreen \
QT_QPA_PLATFORMTHEME= \
QT_STYLE_OVERRIDE=Fusion \
MAKEFLAGS=-j2 \
CMAKE_BUILD_PARALLEL_LEVEL=2 \
makepkg --cleanbuild --clean --noconfirm
fresh_package="$package_work/arch/zenpdf-git-0.1.0.r0.gaa118d8-1-x86_64.pkg.tar.zst"
test "$(bsdtar -xOf "$fresh_package" usr/bin/zenpdf | sha256sum | cut -d' ' -f1)" = \
  a48f20268555796a7f159508a27cd6a36c2561a936cfb7767c5a4ea072292416
python3 "$evidence_repo/scripts/acceptance/l005_package_manifest.py" \
  "$fresh_package" "$package_work/fresh-members.json"
python3 - "$package_work/fresh-members.json" <<'PY'
import json, sys
from pathlib import Path

members = json.loads(Path(sys.argv[1]).read_text())["members"]
actual = {member["path"]: member["sha256"] for member in members}
metadata = {".BUILDINFO", ".MTREE", ".PKGINFO"}
payload = {
    "usr/bin/zenpdf": "a48f20268555796a7f159508a27cd6a36c2561a936cfb7767c5a4ea072292416",
    "usr/bin/zenpdf-launch": "5354a8f20b27f088cb44d256d984024ddbac669e97879b30dd5d132d3d8fe21e",
    "usr/share/applications/io.github.rohan-patnaik.zenpdf.desktop": "713b854417ddf04853e2b960348cd7010abbd182188d96c9010969ab14faf398",
    "usr/share/licenses/zenpdf-git/LICENSE": "0d96a4ff68ad6d4b6f1f30f713b18d5184912ba8dd389f86aa7710db079abcb0",
    "usr/share/metainfo/io.github.rohan-patnaik.zenpdf.metainfo.xml": "5a6652bf9e2be8541be9bfa7a8a790d1f109fab4b57ebc1831caf8bebd573d9d",
}
assert set(actual) == metadata | set(payload)
assert {name: actual[name] for name in payload} == payload
PY
```

Install the retained accepted package into a task-owned pacman root as an unprivileged user mapped to namespace root. Use the same config for install/query/integrity and leave the host package untouched:

```sh
package_root="$package_work/pacman-root"
mkdir -p "$package_root/var/lib/pacman" \
  "$package_root/var/cache/pacman/pkg" "$package_root/etc"
pacman_config="$package_root/etc/pacman.conf"
cp /etc/pacman.conf "$pacman_config"
sed -i 's/^DownloadUser = .*/DownloadUser = root/' "$pacman_config"
test "$(grep -cFx 'DownloadUser = root' "$pacman_config")" = 1
unshare --user --map-root-user sh -eu -c '
  root=$1 config=$2 package=$3
  test -d "$root/var/cache/pacman/pkg" && test -w "$root/var/cache/pacman/pkg"
  pacman --root "$root" --dbpath "$root/var/lib/pacman" \
    --cachedir "$root/var/cache/pacman/pkg" --config "$config" \
    -U --noconfirm --nodeps --nodeps "$package"
  test "$(pacman --root "$root" --dbpath "$root/var/lib/pacman" \
    --cachedir "$root/var/cache/pacman/pkg" --config "$config" \
    -Q zenpdf-git)" = "zenpdf-git 0.1.0.r0.gaa118d8-1"
  pacman --root "$root" --dbpath "$root/var/lib/pacman" \
    --cachedir "$root/var/cache/pacman/pkg" --config "$config" \
    -Qkk zenpdf-git 2>&1 | grep -F \
    "zenpdf-git: 12 total files, 0 altered files"
' sh "$package_root" "$pacman_config" "$accepted_package"
test "$(sha256sum "$package_root/usr/bin/zenpdf" | cut -d' ' -f1)" = \
  a48f20268555796a7f159508a27cd6a36c2561a936cfb7767c5a4ea072292416
```

Compile the exact evidence-commit interposer sources and verify each version map permits one defined function export:

```sh
g++ -std=c++23 -O2 -shared -fPIC \
  "$evidence_repo/scripts/acceptance/l005_probe.cpp" \
  $(pkg-config --cflags --libs Qt6Widgets) \
  -Wl,--version-script="$evidence_repo/scripts/acceptance/l005_probe.map" \
  -o "$package_work/l005_probe.so"
gcc -std=c17 -O2 -shared -fPIC \
  "$evidence_repo/scripts/acceptance/l005_fstat_fault.c" -ldl \
  -Wl,--version-script="$evidence_repo/scripts/acceptance/l005_fstat_fault.map" \
  -o "$package_work/l005_fstat_fault.so"
test "$(sha256sum "$package_work/l005_fstat_fault.so" | cut -d' ' -f1)" = \
  bf1fd492ce5bd324c380542ba3027a5e06e7e3d5caf53b05f9043bc12ff5a763
test "$(readelf --dyn-syms --wide "$package_work/l005_probe.so" | \
  awk '$4 == "FUNC" && $5 == "GLOBAL" && $7 != "UND" {print $8}')" = \
  '_ZN12QApplication4execEv@@Qt_6'
test "$(readelf --dyn-syms --wide "$package_work/l005_fstat_fault.so" | \
  awk '$4 == "FUNC" && $5 == "GLOBAL" && $7 != "UND" {print $8}')" = \
  'fstat@@GLIBC_2.33'
```

The accepted compiled probe hash was `1a6bf3c45df6766763b2a60f26c5d5c8355baa16aec12a378ccb838c46eaa260`. Qt embeds the probe source spelling in that binary, so a different acceptance-root pathname can change its binary hash even when the source/map hashes, toolchain, behavior, and sole defined export agree. The `fstat` helper rebuild is byte-identical with the command above.

Populate a caller-provided, task-owned private bundle on host-backed Btrfs with retained accepted artifacts and exact evidence-commit sources; do not place it below the temporary `package_work` tree. The runner requires the accepted compiled probe hashes and acquires `/tmp/zenpdf-package-ui-acceptance.lock`. Run it as the normal task user, matching the accepted run; it creates its bounded mapped-root pacman and bubblewrap namespaces internally. Point `--source-clone` only at the detached clean implementation lane:

```sh
acceptance_root=/path/to/task-owned-btrfs/private-acceptance
approved_fixture=/path/to/approved-three-pages.pdf
accepted_probe_so=/path/to/accepted/l005_probe.so
accepted_fstat_so=/path/to/accepted/l005_fstat_fault.so
umask 077
install -d -m 0700 "$acceptance_root"
test "$(stat -f -c %T "$acceptance_root")" = btrfs
test "$(stat -c %u "$acceptance_root")" = "$(id -u)"
test "$(stat -c %a "$acceptance_root")" = 700
test "$(sha256sum "$approved_fixture" | cut -d' ' -f1)" = \
  893fd90c2553f6a0b075711da52d0433ba73345b0f4f0df2e3f2ded1f3805122
test "$(sha256sum "$rollback_package" | cut -d' ' -f1)" = \
  d231fc58f62847fbf2603b9b29dcf54334457d83b47a3a0cc2ba88b67b6402e2
test "$(sha256sum "$accepted_probe_so" | cut -d' ' -f1)" = \
  1a6bf3c45df6766763b2a60f26c5d5c8355baa16aec12a378ccb838c46eaa260
test "$(sha256sum "$accepted_fstat_so" | cut -d' ' -f1)" = \
  bf1fd492ce5bd324c380542ba3027a5e06e7e3d5caf53b05f9043bc12ff5a763
mkdir -p "$acceptance_root"/{artifacts,fixtures,helpers,cases,records}
cp "$accepted_package" \
  "$acceptance_root/artifacts/zenpdf-git-0.1.0.r0.gaa118d8-1-x86_64.pkg.tar.zst"
cp "$accepted_source_archive" \
  "$acceptance_root/artifacts/zenpdf-source-$sha.tar.gz"
cp "$rollback_package" \
  "$acceptance_root/artifacts/zenpdf-git-0.1.0.r0.geeba33d-1-x86_64.pkg.tar.zst"
cp "$approved_fixture" "$acceptance_root/fixtures/approved-three-pages.pdf"
cp "$evidence_repo/scripts/acceptance/l005_probe.cpp" \
  "$evidence_repo/scripts/acceptance/l005_probe.map" "$accepted_probe_so" \
  "$evidence_repo/scripts/acceptance/l005_fstat_fault.c" \
  "$evidence_repo/scripts/acceptance/l005_fstat_fault.map" "$accepted_fstat_so" \
  "$evidence_repo/scripts/acceptance/l005_installed_acceptance.py" \
  "$evidence_repo/scripts/acceptance/l005_bundle_manifest.py" \
  "$evidence_repo/scripts/acceptance/l005_package_manifest.py" \
  "$acceptance_root/helpers/"
python3 "$evidence_repo/scripts/acceptance/l005_installed_acceptance.py" \
  --acceptance-root "$acceptance_root" \
  --package-root "$package_root" \
  --source-clone "$implementation_repo" \
  --rollback-package "$rollback_package"
```

Close the bundle without a hash cycle: create both member manifests, create the bundle manifest while its self/index exclusions are absent, create the final index last, then re-check the manifest. The index payload follows the schema emitted by the frozen accepted run and hashes the raw record, both member manifests, and bundle manifest.

```sh
record_dir=$(find "$acceptance_root/records" -mindepth 1 -maxdepth 1 -type d \
  -name 'installed-*' | sort | tail -1)
test -n "$record_dir"
python3 "$evidence_repo/scripts/acceptance/l005_package_manifest.py" \
  "$accepted_package" "$record_dir/l005-package-member-manifest.json"
python3 "$evidence_repo/scripts/acceptance/l005_package_manifest.py" \
  "$rollback_package" "$record_dir/l005-rollback-package-member-manifest.json"
python3 "$evidence_repo/scripts/acceptance/l005_bundle_manifest.py" \
  "$acceptance_root" "$record_dir/l005-private-bundle-manifest.json"
test ! -e "$record_dir/l005-evidence-index.json"
python3 - "$record_dir" <<'PY'
import hashlib, json, os, sys
from pathlib import Path

record = Path(sys.argv[1])
root = record.parents[1]
def digest(name):
    return hashlib.sha256((record / name).read_bytes()).hexdigest()
def root_digest(name):
    return hashlib.sha256((root / name).read_bytes()).hexdigest()
payload = {
    "schema": 1,
    "result": "PASS",
    "authoritative_run": record.name,
    "implementation": {
        "sha": "aa118d8a9544e4982ef8110988f95e45fa9ccf3b",
        "tree": "649c6283b40f22b672f3f4f436ddab03b9809051",
    },
    "artifacts": {
        "approved_fixture": root_digest("fixtures/approved-three-pages.pdf"),
        "candidate_package": root_digest("artifacts/zenpdf-git-0.1.0.r0.gaa118d8-1-x86_64.pkg.tar.zst"),
        "exact_source_archive": root_digest("artifacts/zenpdf-source-aa118d8a9544e4982ef8110988f95e45fa9ccf3b.tar.gz"),
        "rollback_package": root_digest("artifacts/zenpdf-git-0.1.0.r0.geeba33d-1-x86_64.pkg.tar.zst"),
    },
    "helpers": {
        name: root_digest("helpers/" + name) for name in (
            "l005_bundle_manifest.py", "l005_fstat_fault.c",
            "l005_fstat_fault.map", "l005_fstat_fault.so",
            "l005_installed_acceptance.py", "l005_package_manifest.py",
            "l005_probe.cpp", "l005_probe.map", "l005_probe.so",
        )
    },
    "records": {
        name: digest(name) for name in (
            "l005-installed-acceptance.json",
            "l005-package-member-manifest.json",
            "l005-private-bundle-manifest.json",
            "l005-rollback-package-member-manifest.json",
        )
    },
}
data = (json.dumps(payload, indent=2, sort_keys=True) + "\n").encode()
fd = os.open(record / "l005-evidence-index.json", os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
try:
    os.write(fd, data)
    os.fsync(fd)
finally:
    os.close(fd)
PY
python3 "$evidence_repo/scripts/acceptance/l005_bundle_manifest.py" --check \
  "$acceptance_root" "$record_dir/l005-private-bundle-manifest.json"
```

The runner deliberately writes private paths, runtime identifiers, precise journal times, raw stderr, and fixture-derived state into its private 0700 bundle. Do not commit that output. Only the path-free aggregate audit is in this repository. The committed helper hashes are `49943b4b...`/`826350a5...` for the Qt probe, `3c83918d...`/`a25b8212...` for the `fstat` fault, `405116bf...` for the runner, `b4d8d0ec...` for the bundle manifest helper, and `ddcf38b5...` for the package manifest helper; full values are in the machine-readable audit.
