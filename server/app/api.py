"""Read API consumed by the Flutter app."""
import hmac
import logging
import time
from typing import Any, Dict, List, Optional

from fastapi import APIRouter, Header, HTTPException, Query

from . import collector, db
from .config import config

log = logging.getLogger("hexair.api")

router = APIRouter()


def _require_auth(authorization: Optional[str]) -> None:
    if not authorization or not authorization.startswith("Bearer "):
        raise HTTPException(
            status_code=401,
            detail="Missing bearer token",
            headers={"WWW-Authenticate": 'Bearer realm="hexair"'},
        )
    presented = authorization[len("Bearer "):]
    # Constant-time: the token is the only thing guarding the history.
    if not hmac.compare_digest(presented, config.api_token):
        raise HTTPException(status_code=403, detail="Forbidden")


def _num(row: Dict[str, Any], key: str) -> float:
    """Null-safe numeric read.

    The Flutter model's nested parser casts with a non-nullable `as num`, so a
    null here would throw client-side. Buckets with no data for a column are
    reported as 0, matching how the app already treats missing values.
    """
    v = row.get(key)
    if v is None:
        return 0
    return float(v)


def _to_nested(row: Dict[str, Any]) -> Dict[str, Any]:
    """Shape a row the way SensorPayload.fromJson's nested branch expects.

    Deliberately mirrors the WebSocket format rather than /air's flat one:
    the nested branch is the only one that honours an incoming `timestamp`,
    which is exactly what history needs.
    """
    return {
        "timestamp": int(row["ts_ms"]),
        "ens160": {
            "aqi": int(_num(row, "aqi")),
            "eco2": int(_num(row, "eco2")),
            "tvoc": int(_num(row, "tvoc")),
        },
        "aht21": {
            "temperature": _num(row, "temperature"),
            "humidity": _num(row, "humidity"),
        },
        "pms5003": {
            "pm1_0": int(_num(row, "pm1_0")),
            "pm2_5": int(_num(row, "pm2_5")),
            "pm10": int(_num(row, "pm10")),
        },
        "bmp580": {
            # DB stores pressure_hpa/altitude_m; the client model reads
            # pressure/altitude.
            "pressure": _num(row, "pressure_hpa"),
            "altitude": _num(row, "altitude_m"),
        },
        "ready": bool(_num(row, "ready")),
        "uptime_s": int(_num(row, "uptime_s")),
    }


@router.get("/health")
async def health() -> Dict[str, Any]:
    """Unauthenticated liveness probe (pointed at by Uptime Kuma)."""
    now_ms = int(time.time() * 1000)
    last = collector.state.last_success_ms
    age_s = None if last is None else round((now_ms - last) / 1000, 1)
    # Stale if we've missed roughly three polls in a row.
    device_ok = age_s is not None and age_s < config.poll_interval_s * 3

    db_ok = True
    db_error = None
    try:
        await db.stats(config.device_id)
    except Exception as e:  # noqa: BLE001 - health must report, not raise
        db_ok = False
        db_error = f"{type(e).__name__}: {e}"

    from . import mqtt as mqtt_mod
    mqtt_status = {"enabled": config.mqtt_enabled}
    if mqtt_mod.bridge is not None:
        mqtt_status["connected"] = mqtt_mod.bridge.connected

    return {
        "status": "ok" if db_ok else "degraded",
        "database": {"ok": db_ok, "error": db_error},
        "mqtt": mqtt_status,
        "collector": {
            "device_ok": device_ok,
            "last_success_ms": last,
            "last_success_age_s": age_s,
            "consecutive_failures": collector.state.consecutive_failures,
            "samples_written": collector.state.samples_written,
            "last_error": collector.state.last_error,
        },
    }


@router.get("/stats")
async def get_stats(
    device: str = Query(default=None),
    authorization: Optional[str] = Header(default=None),
) -> Dict[str, Any]:
    _require_auth(authorization)
    device_id = device or config.device_id
    row = await db.stats(device_id)
    return {
        "device_id": device_id,
        "rows": int(row.get("rows_total") or 0),
        "first_ms": row.get("first_ms"),
        "last_ms": row.get("last_ms"),
    }


@router.get("/readings")
async def get_readings(
    from_ms: int = Query(alias="from", description="Range start, unix ms"),
    to_ms: int = Query(alias="to", description="Range end, unix ms"),
    bucket: str = Query(
        default="auto",
        description="'auto', 'raw', or a bucket width in seconds",
    ),
    device: str = Query(default=None),
    authorization: Optional[str] = Header(default=None),
) -> Dict[str, Any]:
    _require_auth(authorization)

    if to_ms <= from_ms:
        raise HTTPException(status_code=400, detail="'to' must be after 'from'")

    device_id = device or config.device_id

    if bucket == "auto":
        bucket_s = db.choose_bucket(from_ms, to_ms, config.max_points)
    elif bucket == "raw":
        bucket_s = 0
    else:
        try:
            bucket_s = int(bucket)
        except ValueError:
            raise HTTPException(
                status_code=400,
                detail="'bucket' must be 'auto', 'raw', or an integer",
            ) from None
        if bucket_s < 0:
            raise HTTPException(status_code=400, detail="'bucket' must be >= 0")

    rows: List[Dict[str, Any]] = await db.select_readings(
        device_id, from_ms, to_ms, bucket_s, config.max_points
    )

    return {
        "device_id": device_id,
        "from": from_ms,
        "to": to_ms,
        "bucket_s": bucket_s,
        "count": len(rows),
        "readings": [_to_nested(r) for r in rows],
    }
