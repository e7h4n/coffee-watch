#include "update_manifest.h"

#include <stdio.h>
#include <string.h>

/* This intentionally accepts a strict subset of JSON: the schema has only
 * unsigned integer and printable ASCII string values and needs no escapes. */
typedef struct {
    const char *p;
    const char *end;
} reader_t;

static void whitespace(reader_t *r)
{
    while (r->p < r->end && (*r->p == ' ' || *r->p == '\t' ||
           *r->p == '\r' || *r->p == '\n')) r->p++;
}

static int take(reader_t *r, char c)
{
    whitespace(r);
    if (r->p == r->end || *r->p != c) return -1;
    r->p++;
    return 0;
}

static int string_value(reader_t *r, char *out, size_t cap)
{
    size_t n = 0;
    if (take(r, '"')) return -1;
    while (r->p < r->end && *r->p != '"') {
        unsigned char c = (unsigned char)*r->p++;
        if (c < 0x20 || c > 0x7e || c == '\\' || n + 1 >= cap) return -1;
        out[n++] = (char)c;
    }
    if (r->p == r->end) return -1;
    r->p++;
    out[n] = '\0';
    return 0;
}

int um_content_length(const char *value, size_t len, uint32_t *out)
{
    uint32_t v = 0;
    size_t i;
    if (!value || !out || !len || len > 10) return -1;
    for (i = 0; i < len; ++i) {
        unsigned char c = (unsigned char)value[i];
        uint32_t digit;
        if (c < '0' || c > '9') return -1;
        digit = (uint32_t)(c - '0');
        if (v > (UINT32_MAX - digit) / 10u) return -1;
        v = v * 10u + digit;
    }
    *out = v;
    return 0;
}

static int uint_value(reader_t *r, uint32_t *out)
{
    const char *start;
    whitespace(r);
    start = r->p;
    while (r->p < r->end && *r->p >= '0' && *r->p <= '9') r->p++;
    if (r->p - start > 1 && *start == '0') return -1;
    return um_content_length(start, (size_t)(r->p - start), out);
}

int um_version_build(const char *version, uint32_t *build)
{
    size_t len;
    if (!version || !build) return -1;
    /* Bounded scan also keeps URL generation safe with caller-built structs. */
    for (len = 0; len < 32 && version[len]; ++len) { }
    if (len < 6 || len >= 32 || memcmp(version, "v0.1.", 5)) return -1;
    if (len > 6 && version[5] == '0') return -1;
    return um_content_length(version + 5, len - 5, build);
}

int um_running_version(const char *version, uint32_t *build, int *development)
{
    uint32_t number;
    size_t len, digits, i;
    if (!version || !build || !development) return -1;
    if (!um_version_build(version, &number)) {
        *build = number;
        *development = 0;
        return 0;
    }
    for (len = 0; len <= 46 && version[len]; ++len) { }
    if (len > 46) return -1;
    if (len == 3 && !memcmp(version, "dev", 3)) {
        *build = 0;
        *development = 1;
        return 0;
    }
    digits = len;
    if (len >= 6 && !memcmp(version + len - 6, "-dirty", 6)) digits -= 6;
    if (digits < 7 || digits > 40) return -1;
    for (i = 0; i < digits; ++i)
        if (!((version[i] >= '0' && version[i] <= '9') ||
              (version[i] >= 'a' && version[i] <= 'f'))) return -1;
    *build = 0;
    *development = 1;
    return 0;
}

static int hex_digit(unsigned char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int um_parse(const char *json, size_t len, um_manifest_t *out)
{
    static const char *const keys[] = {
        "schema", "product", "chip", "partition_schema", "version", "tag",
        "bin", "size", "sha256"
    };
    reader_t r;
    um_manifest_t m;
    unsigned seen = 0;
    char key[32], text[81], tag[32] = {0}, expected[80];
    size_t i;
    int field;
    uint32_t number;
    if (!json || !out || !len || len > UM_MANIFEST_MAX) return -1;
    memset(&m, 0, sizeof m);
    r.p = json;
    r.end = json + len;
    if (take(&r, '{')) return -1;
    for (;;) {
        if (string_value(&r, key, sizeof key)) return -1;
        for (field = 0; field < 9; ++field) {
            if (strcmp(key, keys[field]) == 0) break;
        }
        if (field == 9 || (seen & (1u << field)) || take(&r, ':')) return -1;
        seen |= 1u << field;
        if (field == 0 || field == 3 || field == 7) {
            if (uint_value(&r, &number)) return -1;
            if (field == 0 && number != 1) return -1;
            if (field == 3 && number != 2) return -1;
            if (field == 7) {
                if (!number || number > UM_FIRMWARE_MAX) return -1;
                m.size = number;
            }
        } else {
            if (string_value(&r, text, sizeof text)) return -1;
            switch (field) {
            case 1: if (strcmp(text, "coffee-watch")) return -1; break;
            case 2: if (strcmp(text, "esp32s3")) return -1; break;
            case 4:
                if (um_version_build(text, &m.build)) return -1;
                memcpy(m.version, text, strlen(text) + 1);
                break;
            case 5:
                if (strlen(text) >= sizeof tag) return -1;
                memcpy(tag, text, strlen(text) + 1);
                break;
            case 6:
                if (strlen(text) >= sizeof m.bin) return -1;
                memcpy(m.bin, text, strlen(text) + 1);
                break;
            case 8:
                if (strlen(text) != 64) return -1;
                for (i = 0; i < 32; ++i) {
                    int hi = hex_digit((unsigned char)text[i * 2]);
                    int lo = hex_digit((unsigned char)text[i * 2 + 1]);
                    if (hi < 0 || lo < 0) return -1;
                    m.sha256[i] = (uint8_t)((hi << 4) | lo);
                }
                break;
            default: return -1;
            }
        }
        whitespace(&r);
        if (r.p < r.end && *r.p == '}') { r.p++; break; }
        if (take(&r, ',')) return -1;
    }
    whitespace(&r);
    if (r.p != r.end || seen != 0x1ffu || strcmp(tag, m.version)) return -1;
    (void)snprintf(expected, sizeof expected, "coffee-watch-%s.bin", m.version);
    if (strcmp(m.bin, expected)) return -1;
    *out = m;
    return 0;
}

/* No permissive URL library: validation and resolution use exactly the same
 * representation passed to the HTTP client, preventing authority ambiguity. */
static int url_chars(const char *s, size_t *length)
{
    size_t i;
    int query = 0;
    if (!s) return -1;
    for (i = 0; i <= UM_URL_MAX; ++i) {
        unsigned char c = (unsigned char)s[i];
        if (!c) { *length = i; return 0; }
        if (c < 0x21 || c > 0x7e || c == '#' || c == '\\') return -1;
        if (c == '?') query = 1;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || strchr("-._~:/?[]@!$&'()*+,;=%", c))) return -1;
        if (c == '%') {
            int hi, lo, decoded;
            if (i + 2 > UM_URL_MAX || !s[i + 1] || !s[i + 2]) return -1;
            hi = hex_digit((unsigned char)s[i + 1]);
            lo = hex_digit((unsigned char)s[i + 2]);
            if (hi < 0 || lo < 0) return -1;
            decoded = hi * 16 + lo;
            if (decoded < 0x20 || decoded > 0x7e || decoded == '\\' ||
                (!query && (decoded == ' ' || decoded == '.' || decoded == '/' ||
                            decoded == '?' || decoded == '#')))
                return -1;
            i += 2;
        }
    }
    return -1;
}

/* Returns the first byte after the authority and the end of the path. */
static int absolute_url(const char *s, size_t *authority_end, size_t *path_end,
                        size_t *length)
{
    size_t end;
    if (url_chars(s, length) || *length < 8 || memcmp(s, "https://", 8)) return -1;
    end = 8;
    while (end < *length && s[end] != '/' && s[end] != '?') end++;
    if (!((end - 8 == 10 && !memcmp(s + 8, "github.com", 10)) ||
          (end - 8 == 36 && !memcmp(s + 8, "release-assets.githubusercontent.com", 36))))
        return -1;
    *authority_end = end;
    while (end < *length && s[end] != '?') end++;
    *path_end = end;
    return 0;
}

static int append(char *out, size_t *used, const char *s, size_t n)
{
    if (n > UM_URL_MAX - *used) return -1;
    memcpy(out + *used, s, n);
    *used += n;
    out[*used] = '\0';
    return 0;
}

/* Normalize dot segments in place; query bytes are preserved verbatim. */
static int normalize(char *s, size_t authority_end, size_t path_end, size_t len)
{
    size_t read = authority_end, write = authority_end, start, n;
    if (read == path_end) return 0; /* URL with absent path. */
    if (s[read] != '/') return -1;
    s[write++] = '/';
    read++;
    while (read < path_end) {
        int slash;
        start = read;
        while (read < path_end && s[read] != '/') read++;
        n = read - start;
        slash = read < path_end;
        if (n == 1 && s[start] == '.') {
            /* A final dot retains the preceding slash. */
        } else if (n == 2 && s[start] == '.' && s[start + 1] == '.') {
            if (write <= authority_end + 1) return -1;
            if (s[write - 1] == '/') write--;
            while (write > authority_end + 1 && s[write - 1] != '/') write--;
        } else {
            memmove(s + write, s + start, n);
            write += n;
            if (slash) s[write++] = '/';
        }
        if (slash) read++;
    }
    memmove(s + write, s + path_end, len - path_end + 1);
    return 0;
}

int um_resolve_url(const char *base, const char *location, char *out, size_t cap)
{
    char url[UM_URL_MAX + 1];
    size_t ba, bp, bl, ll, a, p, len, used = 0, dir, i;
    if (!out || !cap || absolute_url(base, &ba, &bp, &bl) ||
        url_chars(location, &ll) || !ll) return -1;
    url[0] = '\0';
    if (ll >= 8 && !memcmp(location, "https://", 8)) {
        if (append(url, &used, location, ll)) return -1;
    } else if (ll >= 2 && location[0] == '/' && location[1] == '/') {
        if (append(url, &used, "https:", 6) || append(url, &used, location, ll)) return -1;
    } else {
        /* A colon in the first relative segment would be a new URI scheme. */
        for (i = 0; i < ll && location[i] != '/' && location[i] != '?'; ++i)
            if (location[i] == ':') return -1;
        if (location[0] == '/') {
            if (append(url, &used, base, ba)) return -1;
        } else if (location[0] == '?') {
            if (append(url, &used, base, bp)) return -1;
        } else {
            dir = bp;
            while (dir > ba && base[dir - 1] != '/') dir--;
            if (append(url, &used, base, dir)) return -1;
            if (dir == ba && append(url, &used, "/", 1)) return -1;
        }
        if (append(url, &used, location, ll)) return -1;
    }
    if (absolute_url(url, &a, &p, &len) || normalize(url, a, p, len)) return -1;
    len = strlen(url);
    if (len + 1 > cap) return -1;
    memmove(out, url, len + 1);
    return 0;
}

int um_firmware_url(const um_manifest_t *manifest, char *out, size_t cap)
{
    char expected[80], url[256];
    uint32_t build;
    int n;
    size_t i;
    if (!manifest || !out || !cap || um_version_build(manifest->version, &build) ||
        build != manifest->build || !manifest->size || manifest->size > UM_FIRMWARE_MAX)
        return -1;
    for (i = 0; i < sizeof manifest->bin && manifest->bin[i]; ++i) { }
    if (i == sizeof manifest->bin) return -1;
    (void)snprintf(expected, sizeof expected, "coffee-watch-%s.bin", manifest->version);
    if (strcmp(expected, manifest->bin)) return -1;
    n = snprintf(url, sizeof url,
                 "https://github.com/e7h4n/coffee-watch/releases/download/%s/%s",
                 manifest->version, manifest->bin);
    if (n < 0 || (size_t)n >= sizeof url || (size_t)n + 1 > cap) return -1;
    memmove(out, url, (size_t)n + 1);
    return 0;
}
