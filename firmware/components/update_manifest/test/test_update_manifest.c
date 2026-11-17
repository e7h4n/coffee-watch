/* Standalone deterministic robustness harness. Run under ASan/UBSan as well as
 * ordinary native builds; uses tightly sized allocations to expose overreads. */
#include "update_manifest.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t state = 0x739af23du;
static uint32_t random_u32(void)
{
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

int main(void)
{
    static const char valid[] =
        "{\"schema\":1,\"product\":\"coffee-watch\",\"chip\":\"esp32s3\","
        "\"partition_schema\":2,\"version\":\"v0.1.42\",\"tag\":\"v0.1.42\","
        "\"bin\":\"coffee-watch-v0.1.42.bin\",\"size\":123456,"
        "\"sha256\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\"}";
    const char *base = "https://github.com/a/b/file?old=1";
    unsigned iteration, byte;
    size_t n, i;
    um_manifest_t out, before;
    char url[UM_URL_MAX + 1];
    uint32_t number;
    int development;
    assert(um_parse(valid, sizeof valid - 1, &out) == 0);
    assert(out.build == 42);
    /* Every one-byte mutation exercises actual parser logic, not a mock. */
    for (i = 0; i < sizeof valid - 1; ++i) {
        for (byte = 0; byte < 256; ++byte) {
            char copy[sizeof valid];
            memcpy(copy, valid, sizeof valid);
            copy[i] = (char)byte;
            memset(&out, 0xa5, sizeof out);
            before = out;
            if (um_parse(copy, sizeof valid - 1, &out))
                assert(memcmp(&out, &before, sizeof out) == 0);
            else {
                assert(out.size > 0 && out.size <= UM_FIRMWARE_MAX);
                assert(um_firmware_url(&out, url, sizeof url) == 0);
            }
        }
    }
    for (iteration = 0; iteration < 30000; ++iteration) {
        char *input;
        n = random_u32() % (UM_MANIFEST_MAX + 2u);
        input = malloc(n + 1);
        assert(input);
        for (i = 0; i < n; ++i) input[i] = (char)(random_u32() & 0xff);
        input[n] = '\0';
        memset(&out, 0xa5, sizeof out);
        before = out;
        if (um_parse(input, n, &out)) assert(memcmp(&out, &before, sizeof out) == 0);
        (void)um_resolve_url(base, input, url, sizeof url);
        (void)um_version_build(input, &number);
        (void)um_running_version(input, &number, &development);
        (void)um_content_length(input, n, &number);
        free(input);
    }
    /* Parsers must never require a sentinel byte beyond their explicit span. */
    for (n = 1; n < sizeof valid; ++n) {
        char *exact = malloc(n);
        assert(exact);
        memcpy(exact, valid, n);
        (void)um_parse(exact, n, &out);
        free(exact);
    }
    /* Both legal output/input alias cases are supported. */
    strcpy(url, base);
    assert(um_resolve_url(url, "../next", url, sizeof url) == 0);
    assert(!strcmp(url, "https://github.com/a/next"));
    strcpy(url, "/new");
    assert(um_resolve_url(base, url, url, sizeof url) == 0);
    assert(!strcmp(url, "https://github.com/new"));
    puts("update_manifest: mutation, random-input, span and alias checks passed");
    return 0;
}
