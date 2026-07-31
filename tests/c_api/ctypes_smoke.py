#!/usr/bin/env python3
"""Smoke test: drive the mog C API from Python via ctypes.

This proves the shared library is loadable and callable through a foreign
function interface, which is the primary reason the C binding exists. It starts a
loopback HTTP server, performs a GET through the C API using the embedded
backend, and checks the status, body, and a header.

Usage: ctypes_smoke.py <path-to-shared-library>
"""
import ctypes
import sys
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

BODY = b"hello from python"


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def do_GET(self):  # noqa: N802 (name mandated by BaseHTTPRequestHandler)
        self.send_response(200)
        self.send_header("Content-Type", "text/plain")
        self.send_header("Content-Length", str(len(BODY)))
        self.send_header("X-Smoke", "yes")
        self.end_headers()
        self.wfile.write(BODY)

    def log_message(self, *args):  # silence per-request logging
        pass


def load_library(path):
    lib = ctypes.CDLL(path)
    p = ctypes.c_void_p
    cstr = ctypes.c_char_p
    lib.mog_version.restype = cstr
    lib.mog_request_new.restype = p
    lib.mog_request_new.argtypes = [cstr, cstr]
    lib.mog_request_free.argtypes = [p]
    lib.mog_request_set_backend.argtypes = [p, cstr]
    lib.mog_perform.restype = p
    lib.mog_perform.argtypes = [p]
    lib.mog_response_ok.restype = ctypes.c_int
    lib.mog_response_ok.argtypes = [p]
    lib.mog_response_status.restype = ctypes.c_int
    lib.mog_response_status.argtypes = [p]
    lib.mog_response_body.restype = p
    lib.mog_response_body.argtypes = [p, ctypes.POINTER(ctypes.c_size_t)]
    lib.mog_response_header.restype = cstr
    lib.mog_response_header.argtypes = [p, cstr]
    lib.mog_response_error_message.restype = cstr
    lib.mog_response_error_message.argtypes = [p]
    lib.mog_response_free.argtypes = [p]
    return lib


def main():
    if len(sys.argv) != 2:
        print("usage: ctypes_smoke.py <shared-library>", file=sys.stderr)
        return 2

    lib = load_library(sys.argv[1])

    version = lib.mog_version()
    if not version:
        print("mog_version() empty", file=sys.stderr)
        return 1
    print("loaded mog", version.decode())

    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    port = server.server_address[1]
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        url = "http://127.0.0.1:{}/ping".format(port).encode()
        req = lib.mog_request_new(b"GET", url)
        if not req:
            print("mog_request_new failed", file=sys.stderr)
            return 1
        lib.mog_request_set_backend(req, b"embedded")
        resp = lib.mog_perform(req)
        if not resp:
            print("mog_perform returned NULL", file=sys.stderr)
            return 1

        if lib.mog_response_ok(resp) != 1:
            print("request failed:", lib.mog_response_error_message(resp).decode(),
                  file=sys.stderr)
            return 1

        status = lib.mog_response_status(resp)
        if status != 200:
            print("unexpected status", status, file=sys.stderr)
            return 1

        length = ctypes.c_size_t(0)
        ptr = lib.mog_response_body(resp, ctypes.byref(length))
        body = ctypes.string_at(ptr, length.value)
        if body != BODY:
            print("unexpected body", body, file=sys.stderr)
            return 1

        header = lib.mog_response_header(resp, b"x-smoke")
        if header != b"yes":
            print("unexpected header", header, file=sys.stderr)
            return 1

        lib.mog_response_free(resp)
        lib.mog_request_free(req)
        print("ctypes smoke ok: status={} body={!r}".format(status, body))
        return 0
    finally:
        server.shutdown()
        server.server_close()


if __name__ == "__main__":
    sys.exit(main())
