#!/usr/bin/env python3
"""Run a mog HTTP server from Python via ctypes, with a Python route handler.

This proves an FFI consumer can not only call the mog client but also stand up a
server whose route handler is a Python callback (wrapped with CFUNCTYPE, which
ctypes invokes under the GIL). It starts the server on an ephemeral port, fetches
from it with urllib, and checks the handler ran and produced the response.

Usage: ctypes_server.py <path-to-shared-library>
"""
import ctypes
import sys
import urllib.request

BODY = b"handled in python"


def configure(lib):
    p = ctypes.c_void_p
    cstr = ctypes.c_char_p
    lib.mog_server_new.restype = p
    lib.mog_server_free.argtypes = [p]
    lib.mog_server_set_port.argtypes = [p, ctypes.c_ushort]
    lib.mog_server_route.restype = ctypes.c_int
    lib.mog_server_start.restype = ctypes.c_int
    lib.mog_server_start.argtypes = [p]
    lib.mog_server_port.restype = ctypes.c_ushort
    lib.mog_server_port.argtypes = [p]
    lib.mog_server_stop.argtypes = [p]
    lib.mog_server_last_error.restype = cstr
    lib.mog_server_last_error.argtypes = [p]
    lib.mog_server_request_path.restype = cstr
    lib.mog_server_request_path.argtypes = [p]
    lib.mog_server_response_set_status.argtypes = [p, ctypes.c_int]
    lib.mog_server_response_set_body.argtypes = [p, ctypes.c_void_p, ctypes.c_size_t]


HANDLER_TYPE = ctypes.CFUNCTYPE(None, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p)


def main():
    if len(sys.argv) != 2:
        print("usage: ctypes_server.py <shared-library>", file=sys.stderr)
        return 2

    lib = ctypes.CDLL(sys.argv[1])
    configure(lib)

    seen = {}

    def handler(req, resp, _user):
        seen["path"] = lib.mog_server_request_path(req).decode()
        lib.mog_server_response_set_status(resp, 200)
        lib.mog_server_response_set_body(resp, BODY, len(BODY))

    cb = HANDLER_TYPE(handler)  # must outlive the server

    server = lib.mog_server_new()
    if not server:
        print("mog_server_new failed", file=sys.stderr)
        return 1
    lib.mog_server_route.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p,
                                     HANDLER_TYPE, ctypes.c_void_p]
    try:
        if lib.mog_server_route(server, b"GET", b"/hello", cb, None) != 0:
            print("route failed:", lib.mog_server_last_error(server).decode(), file=sys.stderr)
            return 1
        lib.mog_server_set_port(server, 0)
        if lib.mog_server_start(server) != 0:
            print("start failed:", lib.mog_server_last_error(server).decode(), file=sys.stderr)
            return 1

        port = lib.mog_server_port(server)
        with urllib.request.urlopen("http://127.0.0.1:{}/hello".format(port), timeout=5) as r:
            status = r.status
            body = r.read()

        if status != 200:
            print("unexpected status", status, file=sys.stderr)
            return 1
        if body != BODY:
            print("unexpected body", body, file=sys.stderr)
            return 1
        if seen.get("path") != "/hello":
            print("handler did not observe path, saw", seen, file=sys.stderr)
            return 1

        print("ctypes server ok: status={} body={!r}".format(status, body))
        return 0
    finally:
        lib.mog_server_stop(server)
        lib.mog_server_free(server)


if __name__ == "__main__":
    sys.exit(main())
