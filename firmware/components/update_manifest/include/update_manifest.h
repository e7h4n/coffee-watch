#ifndef UPDATE_MANIFEST_H
#define UPDATE_MANIFEST_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UM_MANIFEST_MAX 4096u
#define UM_FIRMWARE_MAX (4u * 1024u * 1024u)
/* Maximum URL bytes, excluding terminating NUL. */
#define UM_URL_MAX 4096u

typedef struct {
    char version[32];
    char bin[80];
    uint8_t sha256[32];
    uint32_t size;
    uint32_t build;
} um_manifest_t;

/* All helpers return 0 on success, -1 on invalid input/insufficient capacity.
 * Outputs remain unchanged on error. No allocation or nonstandard dependencies.
 * Manifest requires exactly: schema=1, product="coffee-watch", chip="esp32s3",
 * partition_schema=2, version, tag, bin, size, sha256. Unknown/duplicate keys
 * (including prerequisite keys) are rejected. JSON strings must be plain ASCII
 * without escapes; versions are canonical v0.1.<uint32> without leading zeros.
 */
int um_parse(const char *json, size_t len, um_manifest_t *out);
int um_version_build(const char *version, uint32_t *build);
/* Returns 0 = recognized, -1 = invalid, like the other helpers.
 * Recognizes canonical releases (development=0), exact "dev", or 7..40
 * lowercase hex git hashes with optional "-dirty" (development=1, build=0).
 * Outputs remain unchanged for unknown strings/null arguments. */
int um_running_version(const char *version, uint32_t *build, int *development);
/* Strict HTTPS; exact lowercase allowed hosts only. Supports absolute URLs,
 * scheme-relative, root-relative, directory-relative and query-only redirects.
 * Dot segments are normalized; attempts to traverse above root are rejected.
 * Base must itself be valid even for an absolute location. */
int um_resolve_url(const char *base, const char *location, char *out, size_t cap);
/* Immutable release URL under github.com/e7h4n/coffee-watch/releases/download/. */
int um_firmware_url(const um_manifest_t *manifest, char *out, size_t cap);
/* Strict decimal HTTP Content-Length: no signs, whitespace, commas or overflow.
 * Zero is parsed successfully; caller must compare against manifest.size. */
int um_content_length(const char *value, size_t len, uint32_t *out);

#ifdef __cplusplus
}
#endif
#endif
