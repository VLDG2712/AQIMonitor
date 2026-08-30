"""Polls the ESP32's /air endpoint and writes each sample to MariaDB.

Pull rather than push: the firmware needs no changes, the poll loop lives on the
always-on Pi where it can be restarted and inspected, and /air's 1 req/sec rate
limit comfortably accommodates the default 30s interval.
"""
import asyncio
import logging
import time
from typing import Any, Dict, Optional

import httpx

from . import db
from .config import config

log = logging.getLogger("hexair.collector")

# Column -> (python type, inclusive max). The DB columns are narrow, and a
# glitching sensor returning an out-of-range value would otherwise abort the
# INSERT under strict mode.
_LIMITS = {
    "eco2": 65535,
    "tvoc": 65535,
    "aqi": 255,
    "pm1_0": 65535,
    "pm2_5": 65535,
    "pm10": 65535,
    "uptime_s": 4294967295,
}


class CollectorState:
    """Snapshot of collector health, surfaced by GET /health."""

    last_success_ms: Optional[int] = None
    last_error: Optional[str] = None
    consecutive_failures: int = 0
    samples_written: int = 0


state = CollectorState()


def _clamp_int(value: Any, ceiling: int) -> Optional[int]:
    if value is None:
        return None
    try:
        n = int(round(float(value)))
    except (TypeError, ValueError):
        return None
    return max(0, min(n, ceiling))


def _as_float(value: Any) -> Optional[float]:
    if value is None:
        return None
    try:
        f = float(value)
    except (TypeError, ValueError):
        return None
    # NaN/inf would round-trip badly through JSON on the way back out.
    if f != f or f in (float("inf"), float("-inf")):
        return None
    return f


def map_payload(payload: Dict[str, Any]) -> Dict[str, Any]:
    """Flat /air JSON -> column values."""
    values: Dict[str, Any] = {}
    for col in ("temperature", "humidity", "pressure_hpa", "altitude_m",
                "bmp_temp", "chip_temp"):
        values[col] = _as_float(payload.get(col))
    for col, ceiling in _LIMITS.items():
        values[col] = _clamp_int(payload.get(col), ceiling)
    values["ready"] = 1 if payload.get("ready") else 0
    return values


async def poll_once(client: httpx.AsyncClient) -> None:
    headers = {}
    if config.device_token:
        headers["Authorization"] = f"Bearer {config.device_token}"

    res = await client.get(config.device_url, headers=headers)
    res.raise_for_status()
    payload = res.json()

    # /air carries no timestamp of its own, so the collector stamps the row at
    # receipt time.
    ts_ms = int(time.time() * 1000)
    await db.insert_reading(config.device_id, ts_ms, map_payload(payload))

    state.last_success_ms = ts_ms
    state.last_error = None
    state.consecutive_failures = 0
    state.samples_written += 1


async def run() -> None:
    """Poll forever. Never raises; a dead device must not kill the API."""
    timeout = httpx.Timeout(config.device_timeout_s)
    async with httpx.AsyncClient(timeout=timeout) as client:
        log.info(
            "collector polling %s every %ss as %s",
            config.device_url, config.poll_interval_s, config.device_id,
        )
        while True:
            started = time.monotonic()
            try:
                await poll_once(client)
            except asyncio.CancelledError:
                raise
            except Exception as e:  # noqa: BLE001 - loop must survive anything
                state.consecutive_failures += 1
                state.last_error = f"{type(e).__name__}: {e}"
                # The ESP32 rebooting or dropping off WiFi is routine; log the
                # first few loudly, then settle down so the journal stays useful.
                if state.consecutive_failures <= 3:
                    log.warning("poll failed (%d): %s",
                                state.consecutive_failures, state.last_error)
                elif state.consecutive_failures % 20 == 0:
                    log.warning("poll still failing (%d): %s",
                                state.consecutive_failures, state.last_error)

            # Hold the cadence regardless of how long the request took.
            elapsed = time.monotonic() - started
            await asyncio.sleep(max(1.0, config.poll_interval_s - elapsed))
