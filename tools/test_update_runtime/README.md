# Actual updater host runtime tests

Run from the repository root:

```sh
python3 tools/test_update_runtime.py
```

Compiles the actual `firmware_update.c` (included directly to reach static paths) and actual `update_manifest.c` with C warnings as errors and UBSan. Requires `cc` and OpenSSL 3 `libcrypto`; override `CC` / `CW_OPENSSL_LIB` if needed (e.g. `CW_OPENSSL_LIB=-lcrypto` on systems with development packages).

All build outputs live in temporary directories. No IDF, hardware, network, credentials, commits or firmware writes are used. Only host memory simulates flash. SHA-256 uses real OpenSSL EVP; IDF image validation, TLS and RTOS locking are mocked. Worker loops execute one operation via a deterministic notification boundary; restart is intercepted with `longjmp`. This is not a concurrency, bootability, flash latency, power-loss or rollback test.

The 16 groups cover streaming short reads, framing/length errors, digest and prefix checks including chip/project/16MB size, explicit boot-selection gating, activation flash rehash, cancellation, real pending-state health API, layout refusal, factory init, request preflight, allocated OTA handle cleanup on begin failure, actual worker/manifest/version paths, operation-scoped HTTP logging, image-prefix deadline refusal, extra otadata rejection, and activation yielding at 64KiB boundaries.

Two security/control-flow regressions found during review are fixed by the implementation owner and now pass in the default suite. They can also be run individually:

```sh
# Must return ERROR rather than READY when the image-prefix read crosses deadline.
CW_REPRO_PREFIX_DEADLINE=1 python3 tools/test_update_runtime.py
# Must reject an extra DATA/OTA partition outside the normal protected slot range.
CW_REPRO_EXTRA_OTADATA=1 python3 tools/test_update_runtime.py
```

Detailed review and hardware limitations: `/tmp/cw-ota-review.md` (task-local report).

Address/undefined/leak sanitizer verification:

```sh
cc -std=gnu11 -Wall -Wextra -Werror -g -fno-omit-frame-pointer \
  -fsanitize=address,undefined -Itools/test_update_runtime/stubs \
  -Ifirmware/components/update_manifest/include tools/test_update_runtime/runtime.c \
  firmware/components/update_manifest/update_manifest.c -l:libcrypto.so.3 \
  -o /tmp/cw-ota-runtime-asan
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
  UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 /tmp/cw-ota-runtime-asan
```
