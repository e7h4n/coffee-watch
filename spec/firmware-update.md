# Coffee Watch firmware updates

## Scope and trust

ESP32-S3, **16 MiB flash**, ESP-IDF **5.5.2**, partition schema **2**. A one-time,
owner-supervised USB migration changes the bootloader and partition table. Later
updates transfer an **application image only** to the inactive slot. Never send a
merged flash dump, bootloader, partition table or USB ZIP through OTA.

V1 uses certificate-verified public **HTTPS plus SHA-256 integrity**. There is **no
publisher signature**, signing key, secure-boot provisioning or eFuse change.
Hashes, MD5 table checksums, image digests and locally authenticated backup
receipts are not publisher authentication. A compromised publisher/account can
replace firmware and its hash. Do not describe this as signed firmware.

## Fixed layout

| Name | Type/subtype | Offset | Size |
|---|---|---:|---:|
| nvs | data/nvs | 0x9000 | 0x6000 |
| phy_init | data/phy | 0xf000 | 0x1000 |
| ota_0 | app/ota_0 | 0x10000 | 0x400000 |
| ota_1 | app/ota_1 | 0x410000 | 0x400000 |
| otadata | data/ota | 0x810000 | 0x2000 |
| storage | data/spiffs (reserved) | 0x812000 | 0x7ee000 |

The old factory app remains at exactly 0x10000, renamed ota_0. Legacy nvs and PHY
addresses/sizes must match exactly; encryption/read-only partition flags are not
accepted by the migration tool. Storage's subtype reserves filesystem data, **not
permission to mount with format-on-failure or to format it**. No migration or
startup formatting is allowed. Durable curve queuing is separate future work.

## CI, release and candidate artifacts

`.github/workflows/firmware.yml` runs native Python/C/C++ tests, then builds IDF
5.5.2 and packages only after validation. Native harnesses run on Ubuntu 24.04
with Clang and warnings-as-errors; real mbedTLS AES-CBC covers the RS decoder.
CI sparse-checks out the pinned IDF commit
`30aaf64524299d3bde422ca9a2848090d1bc5d0f` with only the cJSON/mbedTLS source
submodules for host tests. It sets IDF_PATH and requires **zero skipped tests**.
Every main push creates a public, non-prerelease GitHub Release
`v0.1.<github.run_number>` targeting that exact push commit. Repository visibility
must already be public: CI refuses publication from a private repository rather
than silently claiming public delivery. GitHub chooses latest server-side using
semantic version ordering (`make_latest=legacy`); an older build finishing late
must not be explicitly forced to latest. No concurrency group drops pending
pushes. Do not replace this workflow or reset its run counter without planning
version continuity.

PRs produce the **same usable candidate artifact format**, including USB ZIP,
using that PR workflow run's `PROJECT_VER=v0.1.<run_number>`, but never create a
Release. A later main run has a greater number, allowing a USB-installed candidate
to update normally. A rerun retains its original number and never overwrites an
existing release/tag. Publication atomically creates a new tag ref at the exact
push SHA (existing lightweight/annotated tags are rejected), then uses
`--verify-tag`. API/network uncertainty fails closed. Repository maintainers
should protect release tags from external mutation; CI does not change tag rules. An interrupted draft/upload requires maintainer review;
there is no automatic mutation/retry. PR jobs have read-only tokens, no deployment
secrets, no persisted checkout credentials and no `pull_request_target` trigger.
Only the separate trusted main publish job has contents-write and executes no
checked-out code. Action revisions and the IDF version are pinned.

Both app and USB bundle are published, as four top-level assets:

- `coffee-watch-v0.1.N.bin`: complete, unsigned **app-only** ESP image, <= 4194304 bytes.
- `latest.json`: strict manifest below.
- `SHA256SUMS`: SHA-256 of the app, manifest and USB ZIP.
- `coffee-watch-v0.1.N-usb-migration.zip`: USB-only bootloader, partition table,
  app, all-FF `ota_data_initial.bin`, `migration.json` and `USB-ONLY.txt`.

No merged binary is generated. The ZIP metadata binds per-file sizes/hashes and
records IDF, rollback and layout compatibility. The packager checks the generated
sdkconfig, exact table, header/chip/flash size, image checksum/digest, embedded
project (`coffee_watch_demo`), embedded IDF version, and exact compiled version.
It also requires rollback, the certificate bundle with certificate date checking
(`CONFIG_MBEDTLS_HAVE_TIME_DATE=y`), panic task-watchdog and 20s watchdog timeout, plus the validated network memory defaults: external TLS
allocations, hardware AES off, Wi-Fi/LwIP PSRAM allocation enabled, static TX
buffers enabled with exactly four buffers. Generated configs missing these
settings fail packaging rather than bypassing the validated memory budget. Local builds default explicitly to `PROJECT_VER=dev`, never a
Git hash or dirty-state pseudo-release. Merely renaming a local app is insufficient.

Example reproducible packaging (no device):

```sh
. "$IDF_PATH/export.sh"  # must report ESP-IDF v5.5.2
python3 firmware/demo/fetch_drivers.py
cd firmware/demo
# Use a fresh SDKCONFIG path so a stale generated config cannot override defaults.
idf.py -B /tmp/cw-build -DSDKCONFIG=/tmp/cw-sdkconfig \
  -DIDF_TARGET=esp32s3 -DPROJECT_VER=v0.1.123 build
cd ../..
python3 tools/package_firmware.py --build-dir /tmp/cw-build \
  --out /tmp/cw-assets --version v0.1.123
```

`--out` must be empty. Production release numbers come from CI, not that example.

### Manifest schema 1 (exact fields, no extensions)

```json
{
  "schema": 1,
  "product": "coffee-watch",
  "chip": "esp32s3",
  "partition_schema": 2,
  "version": "v0.1.123",
  "tag": "v0.1.123",
  "bin": "coffee-watch-v0.1.123.bin",
  "size": 2345678,
  "sha256": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
}
```

The example size/hash are illustrative, not an artifact. Numeric fields must be
integers, not strings/bools; SHA-256 is 64 hex characters (the packager writes lowercase). Version/tag
must be identical canonical `v0.1.N` (no leading zero except N=0), N <= 4294967295. Filename must
match the version exactly, with no URL/path. An updater derives the app URL from
its fixed GitHub release origin/tag and rejects incompatible product/chip/layout,
non-newer numeric versions, oversized/truncated responses and digest mismatches.
No credentials, alternate hosts or arbitrary manifest download URLs are supported.

## Watch controls, transport and data protection

Home → **Firmware** opens a user-requested check. **Download** writes only the
inactive slot; it never changes boot selection. **Restart & install** is a
separate explicit confirmation. There are no automatic checks, downloads or
restarts. Serial `update open`, `update status` and `update cancel` share the same
modal/idle protections; serial offers no install bypass.

Entry requires Home and an idle flow. The Dropbox maintenance gate atomically
reserves admission only when no initialization, reserved/queued command or active
upload remains, and no unconfirmed brew/color revision or ambiguous write is
retained. A cached trace whose CSV **and draft JSON are already confirmed** is
not an unsaved upload: returning Home may then allow OTA without discarding that
cache. A resumed draft or later complete attempt becomes unconfirmed again until
its own JSON is reliably confirmed. Failed/ambiguous work is never silently
cleared to permit an update. This is a volatile-worker guard, not a durable outbox.

While the modal is open, scale/color auto-entry, manual brew/color actions and
Home/Back state resets cannot bypass it. Back/Home requests cancellation, then
keeps the modal and Dropbox admission until the worker reports idle **after
cleanup**. Idle shutdown is suspended while busy. Download and activation require
30–100% measured battery. Below 20% or an unknown/invalid reading cancels busy
work; the board does not measure USB power presence, so a cable is not an override.

Cold-clock checks synchronize the already-initialized shared SNTP service before
any HTTPS, without requiring Dropbox authorization. Synchronization uses at most
15 one-second waits, with cancellation and actual time rechecked. Clock plus
manifest share a 30-second deadline; download has a 300-second deadline. Individual
8-second HTTP/TLS calls and flash operations can defer cooperative cancellation,
so these are not hard preemptive deadlines. TLS verifies the certificate bundle,
hostname and dates. Redirects are limited to HTTPS `github.com` and
`release-assets.githubusercontent.com`, at most five hops, with no credentials.
Signed asset query strings are never logged.

The strict parser is bounded and heapless. Explicit URL/response/manifest/stream
and activation buffers use 8-bit PSRAM only, with no internal fallback; stream
chunks are at most 4 KiB and HTTP buffers 1 KiB. Instrumented host tests measure
20,507 bytes peak explicit OTA buffers, **not total ESP32 RAM use**. The permanent
OTA worker still needs an 8 KiB internal stack because it writes flash. Real
internal-heap/largest-block and task-stack high-water measurements, full Dropbox
upload regression and watchdog timing remain hardware acceptance items.

## Boot selection and health validation: two different paths

Rollback (`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`) is enabled. Main subscribes
to the task watchdog **before NVS/board/network startup**, then must heartbeat; defaults enable/init the task watchdog,
panic on timeout, with a 20-second timeout. A later OTA candidate marked NEW by
`esp_ota_set_boot_partition()` becomes PENDING_VERIFY at its first boot. Firmware
must mark it valid **only after** successful NVS startup, a working UI frame/canvas
and at least 10 seconds of healthy main event-loop heartbeats. Network/BLE success
is not a health requirement. Do not clear pending state just because startup
entered main. A reboot/panic while still pending makes the bootloader mark that
candidate ABORTED and try the prior valid slot. A watchdog is necessary because a
hung main task does not itself cause a rollback without a reset. Exact watchdog
subscription/heartbeat and health calls are implemented in `main.cpp`, not by
these packaging tools. The UI health proxy is a completed frame presentation
and the expected 468×466 canvas; visible pixels still need physical acceptance.

**First USB bootstrap is different:** with both otadata sectors erased and **no
factory partition**, IDF 5.5.2 first tries ota_0. After successfully loading it, the
bootloader initializes its otadata entry as **VALID**, before application health
checks. It is **not** a pending trial boot and cannot roll back to the legacy
layout/image. ota_1 is deliberately left erased. A failed first bootstrap requires
USB recovery. Do not manufacture NEW records or CRCs to claim otherwise.

Verified against pinned official source:

- [bootloader_utility.c, erased selection](https://github.com/espressif/esp-idf/blob/v5.5.2/components/bootloader_support/src/bootloader_utility.c#L379-L469):
  no factory => `No factory image, trying OTA 0`; prior pending => ABORTED;
  NEW => PENDING_VERIFY on ordinary OTA boot.
- [initial selection becomes VALID](https://github.com/espressif/esp-idf/blob/v5.5.2/components/bootloader_support/src/bootloader_utility.c#L489-L505)
  and [image search/load](https://github.com/espressif/esp-idf/blob/v5.5.2/components/bootloader_support/src/bootloader_utility.c#L594-L620).
- [esp_ota_ops.c, NEW selection](https://github.com/espressif/esp-idf/blob/v5.5.2/components/app_update/esp_ota_ops.c#L113-L121)
  and [set boot partition validation](https://github.com/espressif/esp-idf/blob/v5.5.2/components/app_update/esp_ota_ops.c#L552-L638).
- [Image header](https://github.com/espressif/esp-idf/blob/v5.5.2/components/bootloader_support/include/esp_app_format.h#L82-L126),
  [app descriptor](https://github.com/espressif/esp-idf/blob/v5.5.2/components/esp_app_format/include/esp_app_desc.h#L21-L47),
  [partition generator](https://github.com/espressif/esp-idf/blob/v5.5.2/components/partition_table/gen_esp32part.py#L386-L430).

These are source checks, not evidence of a real-device power-cut/rollback test.

## First USB migration (owner present, explicit separate authorization)

**Migration is not atomic.** A cable/power loss between bootloader/table/app/data
writes can leave an unbootable device. USB ROM download recovery remains the
recovery route on supported unsecured devices. Maintain stable power, a tested
USB data cable and physical access to the board's ROM download controls. Never
perform this during an active roast/measurement or without the owner present.
No device operation is implied by a PR review or a passing simulated test.

Use Python 3.10+ with **esptool 4.11.0**, pinned for its command/output semantics:

```sh
python3 -m venv /tmp/cw-usb-env
. /tmp/cw-usb-env/bin/activate
python3 -m pip install esptool==4.11.0
```

Obtain the candidate/Release USB ZIP and its SHA256SUMS through the trusted GitHub
HTTPS artifact/release channel. Verify the checksum, independently set the ZIP
hash below, and review the intended version. Hash from an untrusted ZIP alone is
not a trusted input. Do not copy the following placeholders as real values.

```sh
python3 tools/migrate_ota.py --port /dev/cu.usbmodemYOUR_DEVICE \
  --bundle /path/coffee-watch-v0.1.N-usb-migration.zip \
  --bundle-sha256 REPLACE_WITH_VERIFIED_64_LOWERCASE_HEX \
  --backup-root /private/offline/coffee-watch-backups
```

Default is **inspect/dry-run, no flash writes**. Before any serial access, a
controlled subprocess adapter enforces exactly esptool 4.11.0 and sets its actual
`ESPLoader.WRITE_FLASH_ATTEMPTS=1` and `loader.WRITE_BLOCK_ATTEMPTS=1`.
Stock esptool defaults to two file attempts and three block attempts, and may
reconnect/reset/replay a partially completed file; invoking the CLI once alone
would not provide this guarantee. Every subprocess reapplies this policy before
serial access. The default baud is 460800; `--baud 115200`/230400/921600 may be
selected explicitly, with no automatic fallback/replay. Full-flash reads can take
minutes and have a 30-minute operation timeout.

It enters USB download mode, reads
flash size/security information and all 16MiB, then saves verified backups. It
does not reset into the application on exit. This still requires owner approval
for connecting/resetting/reading a real device. Manually reset after a read-only
inspection if returning to normal operation.

The tool refuses write approval unless all checks pass:

1. Explicit ESP32-S3 connection, observed exactly 16MB, security flags exactly zero,
   explicit Secure Boot/Flash Encryption Disabled. Unknown output fails closed.
   No security provisioning, encryption, anti-rollback, virtual-eFuse or validation
   bypass config is allowed by packaging. It never invokes eFuse commands.
2. Exactly the old NVS/PHY/factory table, checksum protected, with factory at
   0x10000/4MiB and no unknown entries/flags. Schema-2 or partially changed tables
   are **not** treated as resumable first migrations.
3. Original bootloader and app have valid image checksums/digests; embedded app
   project is coffee_watch_demo and IDF is v5.5.2. Candidate flash mode/frequency,
   silicon revision bounds and app eFuse-block/MMU requirements must match the
   observed originals. Unknown/older variants stop for engineering review; do not
   weaken checks just to flash a board.
4. Entire 0x410000..0x1000000 is FF, including ota_1, otadata and reserved storage.
   **Any existing data stops migration**. It never silently formats/wipes it.
5. ZIP digest, all member hashes/sizes, exact partition schema, image descriptors
   and version match. App <=4MiB, bootloader fits below table, otadata exactly FF.
6. Full flash, private NVS and private PHY backups have been persisted, read back
   and SHA-256 verified; a local HMAC-authenticated receipt binds observed USB
   security output, decoded original/target tables, candidate bundle/version and backup hashes immediately before
   writes. Timestamp directory is mode0700, files/key mode0600. Backups include
   Wi-Fi/Dropbox credentials and private curves: **never upload them to GitHub**.

With a reviewed successful inspection, repeat the same command adding **`--apply`**.
It takes a fresh backup and repeats every check; it never trusts stale inspection.
It issues one esptool write operation containing only:

- 0x0 bootloader.bin
- 0x8000 partition-table.bin
- 0x10000 app image
- 0x810000 ota_data_initial.bin (two erased sectors)

No `erase_flash`, storage write, ota_1 write or NVS/PHY write occurs. Esptool erases
only sectors intersecting those four ranges; end-of-image sector padding becomes
FF. After writing, the tool reads back and compares **all 16MiB**, accounting for
that padding, proving all other bytes unchanged before declaring success. It does
not reboot automatically. Manually reset, observe first boot and verify NVS/UI,
then perform a separately supervised subsequent OTA/rollback test. Do not claim
hardware migration or rollback verified until that test is actually done.

On any exception/timeout, **do not blindly rerun**. A write may have happened even
if no receipt was returned. Keep the board in ROM download mode, retain original
backups and diagnose/read current flash before choosing recovery.

### Backup-only / offline evidence

If layout or compatibility evidence is unavailable, collect evidence without
claiming compatibility or writing:

```sh
python3 tools/migrate_ota.py --port /dev/cu.usbmodemYOUR_DEVICE \
  --backup-only --backup-root /private/offline/coffee-watch-backups
# Or inspect an existing full dump, without connecting to a device:
python3 tools/migrate_ota.py --offline-flash /private/original-16MiB.bin \
  --backup-only --backup-root /private/offline/coffee-watch-backups
```

Offline full-dump inspection can also take `--bundle`/`--bundle-sha256`, but it
cannot prove live security configuration and **cannot be combined with --apply**.
Backup-only can save an unrecognized layout but is not permission to migrate it.

Each backup directory has `flash-16MiB.bin`, `nvs-private.bin`, `phy-private.bin`,
`receipt.json` and `receipt.hmac`. Its sibling `TIMESTAMP.receipt-key` is the local
HMAC key: store it separately/offline after verification. This authenticates a
receipt only against the separately protected local key, **not** against an
attacker who can read/replace both the key and backups; it is neither remote
attestation nor a publisher signature.

```sh
python3 tools/migrate_ota.py --verify-backup /private/.../TIMESTAMP \
  --receipt-key /separate/private/TIMESTAMP.receipt-key
```

Do not restore from a backup failing this check. Copy keys and evidence securely;
never print private flash/NVS content or credentials into logs/chat.

### USB recovery to the original layout

Recovery is a separate, destructive operation needing new owner approval. This
tool intentionally does not automatically restore or replay writes. First enter
ROM download mode physically, verify the correct device/16MB/unsecured security
configuration again, and authenticate the **pre-migration** backup above. If its
layout is not the supported factory table, stop for an engineer.

A supervised legacy rollback can restore only the old bootloader region, table
sector and entire old factory slot, without writing NVS/PHY, ota_1 or storage.
Prepare files offline from the authenticated original full dump:

```sh
python3 - /private/.../TIMESTAMP/flash-16MiB.bin /private/recovery-files <<'PY'
from pathlib import Path
import sys
source, target = Path(sys.argv[1]), Path(sys.argv[2])
data = source.read_bytes()
assert len(data) == 0x1000000
target.mkdir(mode=0o700, exist_ok=False)
for name, start, end in [('legacy-boot.bin', 0, 0x8000),
                         ('legacy-table-sector.bin', 0x8000, 0x9000),
                         ('legacy-factory-slot.bin', 0x10000, 0x410000)]:
    path = target / name
    path.write_bytes(data[start:end])
    path.chmod(0o600)
PY
```

Only **after** receipt verification, fresh device identification, explicit owner
approval and inspection of the current partial-write state, an engineer can use
pinned esptool `write_flash --flash_size keep --flash_mode keep --flash_freq keep`
with those three files at **0x0, 0x8000, 0x10000**, respectively, with
`--chip esp32s3 --before default_reset --after no_reset`. Never use `erase_flash`.
Read back full flash afterward, verify restored ranges and unchanged NVS/PHY,
and inspect the restored table before resetting. Old schema ignores residual
new otadata outside its factory slot; do not erase it just to make it look clean.
A future first-migration attempt will correctly refuse that non-erased region,
requiring a separately reviewed recovery plan rather than hidden data deletion.

If the firmware has run since migration, credentials/data may have legitimately
changed. Preserve a fresh full dump too; never overwrite current NVS with an old
NVS backup without a separate data-recovery decision. Full-flash restore is not
part of the normal migration tool. Power failure during recovery is also not
atomic: keep authenticated backups and physical USB recovery available.

## Verification matrix

`python3 -m unittest discover -s tools -p 'test_package_firmware.py' -v` and
`python3 -m unittest discover -s tools -p 'test_migrate_ota.py' -v` run without any
serial device. The migration tests launch a stub esptool module in separate Python
processes, record every argument, simulate sector erasure and compare flash bytes.
Additional tests exercise actual pinned esptool's file- and block-write exception paths with
a fake loader, and execute the actual release shell against a mock gh to verify
atomic tag creation, existing-tag/release refusal and failed lookups/uploads.
They test default no-write, explicit apply with exactly four addresses, backup
permissions/HMAC/hash verification, wrong layout/flash/security/image descriptor,
corrupt/truncated/oversized image and bundle hash, non-erased data, retry refusal,
uncertain writes without replay and offline/backup-only paths. Real-device first
boot, watchdog timing, OTA/rollback and power-cut acceptance remain physical
owner-supervised tests, not implied by native tests or source review.
