/*
 * Pure-C compile and link check for <mog/mog_c.h>.
 *
 * This is compiled by a C (not C++) compiler. It proves the header is valid C
 * and that the C API symbols resolve against the shared library. It makes no
 * network calls, so it is fast and deterministic.
 */
#include <mog/mog_c.h>

#include <stdio.h>
#include <string.h>

int main(void)
{
    const char *version = mog_version();
    if (version == NULL || version[0] == '\0')
    {
        fprintf(stderr, "mog_version() returned empty\n");
        return 1;
    }

    if (strcmp(mog_error_code_name(MOG_OK), "ok") != 0)
    {
        fprintf(stderr, "unexpected error code name\n");
        return 1;
    }

    /* Build a request, exercise a few setters, then release it. */
    mog_request *req = mog_request_new("GET", "http://example.invalid/");
    if (req == NULL)
    {
        fprintf(stderr, "mog_request_new() failed\n");
        return 1;
    }
    mog_request_set_backend(req, "embedded");
    mog_request_set_header(req, "X-Check", "1");
    mog_request_set_timeout_ms(req, 1000);
    mog_request_set_verify_tls(req, 0);
    mog_request_free(req);

    /* Invalid arguments must be rejected without a crash. */
    if (mog_request_new("BOGUS", "http://x/") != NULL)
    {
        fprintf(stderr, "expected NULL for invalid method\n");
        return 1;
    }

    /* NULL-tolerant accessors and frees. */
    mog_response_free(NULL);
    mog_request_free(NULL);
    if (mog_response_ok(NULL) != 0)
    {
        fprintf(stderr, "mog_response_ok(NULL) should be 0\n");
        return 1;
    }

    /* Server C API: create, configure, and free without starting (no I/O). */
    {
        mog_server *server = mog_server_new();
        if (server == NULL)
        {
            fprintf(stderr, "mog_server_new() failed\n");
            return 1;
        }
        mog_server_set_bind_address(server, "127.0.0.1");
        mog_server_set_port(server, 0);
        mog_server_set_threads(server, 2);
        mog_server_serve_files(server, "/", ".", 1);
        if (mog_server_route(server, "GET", "/", NULL, NULL) == 0)
        {
            fprintf(stderr, "expected nonzero for NULL handler\n");
            return 1;
        }
        mog_server_free(server);
        mog_server_free(NULL);
    }

    printf("mog C API ok, version %s\n", version);
    return 0;
}
