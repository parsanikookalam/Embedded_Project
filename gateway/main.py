"""
Thin FastAPI gateway + Swagger UI for Part 2.

All business logic lives in the C HTTPS server. This process only documents
and proxies those APIs (allowed by the project brief).

Swagger UI assets are served locally (gateway/static/) so /docs works offline
even when CDNs are blocked.
"""

from __future__ import annotations

import json
import os
import socket
import ssl
from pathlib import Path
from typing import Any, Dict, Optional, Tuple

import httpx
from dotenv import load_dotenv
from fastapi import FastAPI, HTTPException, Request, Response
from fastapi.openapi.docs import get_swagger_ui_html
from fastapi.responses import HTMLResponse, StreamingResponse
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel, Field

ROOT = Path(__file__).resolve().parents[1]
HERE = Path(__file__).resolve().parent
STATIC_DIR = HERE / "static"
load_dotenv(ROOT / "config.env")

HTTPS_PORT = int(os.getenv("HTTPS_PORT", "8443"))
C_BASE = os.getenv("C_BASE", f"https://127.0.0.1:{HTTPS_PORT}")
VERIFY_TLS = False
GATEWAY_PORT = int(os.getenv("GATEWAY_PORT", "8000"))

app = FastAPI(
    title="Smart Guard System API",
    description=(
        "Swagger gateway for the C HTTPS backend. "
        "Core logic is implemented in C; this layer is documentation + proxy only."
    ),
    version="1.0.0",
    docs_url=None,  # custom /docs with local assets
    redoc_url=None,
)

if STATIC_DIR.is_dir():
    app.mount("/static", StaticFiles(directory=str(STATIC_DIR)), name="static")


class CommandBody(BaseModel):
    cmd: str = Field(
        ...,
        examples=[
            "camera_on",
            "camera_off",
            "guard_on",
            "guard_off",
            "watchdog_on",
            "watchdog_off",
            "thermal_on",
            "thermal_off",
            "test_email",
            "reboot",
        ],
        description="Executable command name",
    )


async def _proxy_json(method: str, path: str, json_body: Optional[Dict[str, Any]] = None):
    """Proxy to C. Send a curl-like request so headers+body fit one SSL_read()."""
    url = f"{C_BASE}{path}"
    headers = {
        "Connection": "close",
        "Accept": "application/json",
        "Accept-Encoding": "identity",
    }
    content: Optional[bytes] = None
    if json_body is not None:
        content = json.dumps(json_body, separators=(",", ":")).encode("utf-8")
        headers["Content-Type"] = "application/json"

    try:
        async with httpx.AsyncClient(verify=VERIFY_TLS, timeout=30.0) as client:
            resp = await client.request(method, url, content=content, headers=headers)
    except httpx.RequestError as exc:
        raise HTTPException(status_code=502, detail=f"C backend unreachable: {exc}") from exc

    content_type = resp.headers.get("content-type", "application/json")
    return Response(content=resp.content, status_code=resp.status_code, media_type=content_type)


def _c_command_http10(cmd: str) -> Tuple[int, bytes, str]:
    """Minimal HTTP/1.0 POST — headers+JSON in one TLS write (Swagger-safe)."""
    body = json.dumps({"cmd": cmd}, separators=(",", ":")).encode("utf-8")
    req = (
        f"POST /api/v1/command HTTP/1.0\r\n"
        f"Host: 127.0.0.1:{HTTPS_PORT}\r\n"
        f"Content-Type: application/json\r\n"
        f"Content-Length: {len(body)}\r\n"
        f"Connection: close\r\n"
        f"\r\n"
    ).encode("ascii") + body

    ctx = ssl._create_unverified_context()
    with socket.create_connection(("127.0.0.1", HTTPS_PORT), timeout=8) as sock:
        with ctx.wrap_socket(sock, server_hostname="127.0.0.1") as ssock:
            ssock.sendall(req)
            chunks = []
            while True:
                try:
                    data = ssock.recv(8192)
                except socket.timeout:
                    break
                if not data:
                    break
                chunks.append(data)
    raw = b"".join(chunks)
    sep = raw.find(b"\r\n\r\n")
    if sep < 0:
        return 502, b'{"error":"bad_upstream"}', "application/json"
    header = raw[:sep].decode("latin1", errors="ignore")
    payload = raw[sep + 4 :]
    status = 502
    try:
        status = int(header.split(" ", 2)[1])
    except Exception:
        pass
    ctype = "application/json"
    for line in header.split("\r\n"):
        if line.lower().startswith("content-type:"):
            ctype = line.split(":", 1)[1].strip()
            break
    return status, payload, ctype


@app.get("/docs", include_in_schema=False)
async def swagger_ui() -> HTMLResponse:
    js_local = STATIC_DIR / "swagger-ui-bundle.js"
    css_local = STATIC_DIR / "swagger-ui.css"
    if js_local.is_file() and css_local.is_file():
        return get_swagger_ui_html(
            openapi_url="/openapi.json",
            title="Smart Guard System API — Swagger",
            swagger_js_url="/static/swagger-ui-bundle.js",
            swagger_css_url="/static/swagger-ui.css",
        )
    # Fallback message if assets were not downloaded yet
    return HTMLResponse(
        "<h2>Swagger UI assets missing</h2>"
        "<p>In WSL run:</p>"
        "<pre>bash ~/embedded_project/scripts/download_swagger_ui.sh\n"
        "sudo systemctl restart api_gateway</pre>"
        '<p>API JSON still works: <a href="/openapi.json">/openapi.json</a></p>',
        status_code=503,
    )


@app.get("/api/v1/telemetry", summary="CPU temp, free memory, CPU load")
async def telemetry():
    return await _proxy_json("GET", "/api/v1/telemetry")


@app.get("/api/v1/persons", summary="Current person count + timestamp")
async def persons():
    return await _proxy_json("GET", "/api/v1/persons")


@app.get("/api/v1/history", summary="Last 5 detection records")
async def history():
    return await _proxy_json("GET", "/api/v1/history")


@app.get("/api/v1/config", summary="Student id/name from config.env")
async def config():
    return await _proxy_json("GET", "/api/v1/config")


@app.get("/api/v1/guard", summary="Part 4: guard / anti-theft arm state")
async def guard():
    return await _proxy_json("GET", "/api/v1/guard")


@app.get("/api/v1/camera", summary="Webcam enabled state (API/dashboard controlled)")
async def camera():
    return await _proxy_json("GET", "/api/v1/camera")


@app.get("/api/v1/watchdog", summary="Part 4: software watchdog enabled")
async def watchdog():
    return await _proxy_json("GET", "/api/v1/watchdog")


@app.get("/api/v1/thermal", summary="Part 4: adaptive thermal management enabled")
async def thermal():
    return await _proxy_json("GET", "/api/v1/thermal")


@app.get("/api/v1/part4", summary="Part 4: guard + watchdog + thermal + camera snapshot")
async def part4():
    return await _proxy_json("GET", "/api/v1/part4")


@app.get("/api/v1/blackbox", summary="Part 4: black-box total human detection events")
async def blackbox():
    return await _proxy_json("GET", "/api/v1/blackbox")


@app.post(
    "/api/v1/command",
    summary="Run extensible device command (reboot, guard_*, camera_*, test_email)",
)
async def command(body: CommandBody):
    # Use tiny HTTP/1.0 client so C always sees {"cmd":...} (httpx often split the body).
    try:
        status, payload, ctype = _c_command_http10(body.cmd)
    except OSError as exc:
        raise HTTPException(status_code=502, detail=f"C backend unreachable: {exc}") from exc
    return Response(content=payload, status_code=status, media_type=ctype)


@app.get("/api/v1/stream", summary="Live MJPEG camera stream")
async def stream(request: Request):
    url = f"{C_BASE}/api/v1/stream"

    async def relay():
        async with httpx.AsyncClient(verify=VERIFY_TLS, timeout=None) as client:
            async with client.stream("GET", url) as resp:
                if resp.status_code != 200:
                    detail = await resp.aread()
                    raise HTTPException(
                        status_code=resp.status_code,
                        detail=detail.decode(errors="ignore"),
                    )
                async for chunk in resp.aiter_bytes():
                    if await request.is_disconnected():
                        break
                    yield chunk

    return StreamingResponse(
        relay(),
        media_type="multipart/x-mixed-replace; boundary=frame",
    )


@app.get("/", include_in_schema=False)
async def root():
    return {
        "service": "Smart Guard Swagger Gateway",
        "docs": "/docs",
        "openapi": "/openapi.json",
        "backend": C_BASE,
        "target": os.getenv("TARGET", "wsl"),
    }


if __name__ == "__main__":
    import uvicorn

    uvicorn.run(app, host="0.0.0.0", port=GATEWAY_PORT, reload=False)
