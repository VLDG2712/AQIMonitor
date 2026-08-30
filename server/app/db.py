"""MariaDB access.

PyMySQL is pure Python, so there is no compile step on aarch64. The workload is
one INSERT per poll interval plus occasional range reads, so a single
lock-guarded connection with reconnect-on-ping is ample; blocking calls are
pushed off the event loop with asyncio.to_thread.
"""
import asyncio
import logging
import threading
from pathlib import Path
from typing import Any, Dict, List, Optional

import pymysql
from pymysql.cursors import DictCursor

from .config import config

log = logging.getLogger("hexair.db")

# Columns written by the collector, in insert order.
COLUMNS = [
    "temperature",
    "humidity",
    "eco2",
    "tvoc",
    "aqi",
    "pressure_hpa",
    "altitude_m",
    "bmp_temp",
    "pm1_0",
    "pm2_5",
    "pm10",
    "chip_temp",
    "ready",
    "uptime_s",
]

# Integer-valued columns get ROUND()ed when averaged so the API doesn't hand
# the phone 3.0000000001 for an AQI.
_INT_COLUMNS = {"eco2", "tvoc", "aqi", "pm1_0", "pm2_5", "pm10", "uptime_s"}

# Granularities the bucketer may choose, seconds. 30s is the native poll rate.
BUCKET_LADDER = [30, 60, 300, 900, 1800, 3600, 10800, 21600, 86400, 604800]

_conn: Optional[pymysql.Connection] = None
_lock = threading.Lock()
_schema_ready = False


def _connect() -> pymysql.Connection:
    return pymysql.connect(
        host=config.db_host,
        port=config.db_port,
        user=config.db_user,
        password=config.db_password,
        database=config.db_name,
        cursorclass=DictCursor,
        autocommit=True,
        connect_timeout=10,
        # Pin UTC so UNIX_TIMESTAMP() bucketing does not depend on the
        # container's or server's local timezone.
        init_command="SET time_zone = '+00:00'",
    )


def _cursor():
    """Return a live connection, reconnecting if the server dropped us."""
    global _conn
    if _conn is None:
        _conn = _connect()
    else:
        _conn.ping(reconnect=True)
    return _conn


def _ensure_schema_sync() -> None:
    """Apply schema.sql. Idempotent, so it can run on every startup.

    Keeps provisioning from ending up half-done: grant.sql creates the database
    and user (which needs root), and the service creates its own table.
    """
    path = Path(__file__).resolve().parent.parent / "schema.sql"
    ddl = path.read_text(encoding="utf-8")
    # Strip comments and split, so a multi-statement file works over a driver
    # that executes one statement at a time.
    stripped = "\n".join(
        line for line in ddl.splitlines() if not line.strip().startswith("--")
    )
    statements = [s.strip() for s in stripped.split(";") if s.strip()]
    with _lock:
        conn = _cursor()
        with conn.cursor() as cur:
            for stmt in statements:
                cur.execute(stmt)
    log.info("schema ready (%d statement(s) applied)", len(statements))


async def ensure_schema() -> None:
    """Create the table if needed, remembering success.

    Retried lazily before the first write, so a service that started while
    MariaDB was still coming up heals itself rather than failing forever.
    """
    global _schema_ready
    if _schema_ready:
        return
    await asyncio.to_thread(_ensure_schema_sync)
    _schema_ready = True


def _insert_sync(device_id: str, ts_ms: int, values: Dict[str, Any]) -> None:
    cols = ", ".join(["device_id", "ts"] + COLUMNS)
    placeholders = ", ".join(["%s", "FROM_UNIXTIME(%s / 1000)"] + ["%s"] * len(COLUMNS))
    sql = f"INSERT IGNORE INTO readings ({cols}) VALUES ({placeholders})"
    params = [device_id, ts_ms] + [values.get(c) for c in COLUMNS]
    with _lock:
        conn = _cursor()
        with conn.cursor() as cur:
            cur.execute(sql, params)


async def insert_reading(device_id: str, ts_ms: int, values: Dict[str, Any]) -> None:
    await ensure_schema()
    await asyncio.to_thread(_insert_sync, device_id, ts_ms, values)


def choose_bucket(from_ms: int, to_ms: int, max_points: int) -> int:
    """Smallest ladder granularity that keeps the result under max_points."""
    span_s = max(1, (to_ms - from_ms) // 1000)
    for b in BUCKET_LADDER:
        if span_s / b <= max_points:
            return b
    return BUCKET_LADDER[-1]


def _select_sync(
    device_id: str, from_ms: int, to_ms: int, bucket_s: int, limit: int
) -> List[Dict[str, Any]]:
    if bucket_s <= 0:
        # Raw rows, still capped so a wide range can't flood the client.
        cols = ", ".join(COLUMNS)
        sql = (
            f"SELECT CAST(UNIX_TIMESTAMP(ts) * 1000 AS SIGNED) AS ts_ms, {cols} "
            "FROM readings "
            "WHERE device_id = %s AND ts >= FROM_UNIXTIME(%s / 1000) "
            "AND ts <= FROM_UNIXTIME(%s / 1000) "
            "ORDER BY ts ASC LIMIT %s"
        )
        params = [device_id, from_ms, to_ms, limit]
    else:
        aggs = []
        for c in COLUMNS:
            if c == "ready":
                # A bucket counts as ready if the sensor was warmed up at any
                # point inside it.
                aggs.append("MAX(ready) AS ready")
            elif c in _INT_COLUMNS:
                aggs.append(f"ROUND(AVG({c})) AS {c}")
            else:
                aggs.append(f"AVG({c}) AS {c}")
        agg_sql = ", ".join(aggs)
        sql = (
            "SELECT CAST(FLOOR(UNIX_TIMESTAMP(ts) / %s) * %s * 1000 AS SIGNED) AS ts_ms, "
            f"{agg_sql} "
            "FROM readings "
            "WHERE device_id = %s AND ts >= FROM_UNIXTIME(%s / 1000) "
            "AND ts <= FROM_UNIXTIME(%s / 1000) "
            "GROUP BY FLOOR(UNIX_TIMESTAMP(ts) / %s) "
            "ORDER BY ts_ms ASC LIMIT %s"
        )
        params = [bucket_s, bucket_s, device_id, from_ms, to_ms, bucket_s, limit]

    with _lock:
        conn = _cursor()
        with conn.cursor() as cur:
            cur.execute(sql, params)
            return list(cur.fetchall())


async def select_readings(
    device_id: str, from_ms: int, to_ms: int, bucket_s: int, limit: int
) -> List[Dict[str, Any]]:
    return await asyncio.to_thread(
        _select_sync, device_id, from_ms, to_ms, bucket_s, limit
    )


def _stats_sync(device_id: str) -> Dict[str, Any]:
    with _lock:
        conn = _cursor()
        with conn.cursor() as cur:
            cur.execute(
                "SELECT COUNT(*) AS rows_total, "
                "CAST(UNIX_TIMESTAMP(MIN(ts)) * 1000 AS SIGNED) AS first_ms, "
                "CAST(UNIX_TIMESTAMP(MAX(ts)) * 1000 AS SIGNED) AS last_ms "
                "FROM readings WHERE device_id = %s",
                [device_id],
            )
            return cur.fetchone() or {}


async def stats(device_id: str) -> Dict[str, Any]:
    return await asyncio.to_thread(_stats_sync, device_id)


def close() -> None:
    global _conn
    with _lock:
        if _conn is not None:
            try:
                _conn.close()
            except Exception:  # noqa: BLE001 - shutdown is best-effort
                pass
            _conn = None
