"""Hexair history service: collector + read API in one process."""
import asyncio
import contextlib
import logging
from contextlib import asynccontextmanager

from fastapi import FastAPI

from . import api, collector, db
from . import mqtt as mqtt_mod
from .config import config

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s %(levelname)-7s %(name)s | %(message)s",
)
log = logging.getLogger("hexair")


@asynccontextmanager
async def lifespan(app: FastAPI):
    config.validate()
    log.info("starting hexair history service")

    # Best-effort: if MariaDB isn't up yet, the collector retries this before
    # its first write, so startup must not hard-fail here.
    try:
        await db.ensure_schema()
    except Exception as e:  # noqa: BLE001 - startup must survive a cold DB
        log.warning("schema not ready at startup (will retry): %s", e)

    if config.mqtt_enabled:
        mqtt_mod.bridge = mqtt_mod.MqttBridge(asyncio.get_running_loop())
        mqtt_mod.bridge.start()
    else:
        log.info("MQTT disabled (set HEXAIR_MQTT_ENABLED=true to enable)")

    task = asyncio.create_task(collector.run(), name="collector")
    try:
        yield
    finally:
        log.info("shutting down")
        task.cancel()
        with contextlib.suppress(asyncio.CancelledError):
            await task
        if mqtt_mod.bridge is not None:
            mqtt_mod.bridge.stop()
        db.close()


app = FastAPI(
    title="Hexair History",
    description="Historical air-quality readings collected from the Hexair ESP32.",
    version="1.0.0",
    lifespan=lifespan,
)
app.include_router(api.router)
