"""Home Assistant MQTT bridge.

Publishes the Hexair's readings as HA entities via MQTT discovery, and accepts
light commands for the NeoPixel ring, translating them into POSTs to the
device's /neo endpoint.

paho runs its own network thread, so callbacks arrive off the event loop.
Anything async (the HTTP call to the device) is marshalled back with
run_coroutine_threadsafe.
"""
import asyncio
import json
import logging
from typing import Any, Dict, Optional

import httpx
import paho.mqtt.client as mqtt

from .config import config

log = logging.getLogger("hexair.mqtt")

# Effect names must line up with the firmware's FX_* constants by index:
# FX_STATIC=0, FX_BREATH=1, FX_SPIN=2, FX_RAINBOW=3, FX_STROBE=4, FX_FIRE=5,
# FX_THEATER=6, FX_SPARKLE=7, FX_COLORCYCLE=8.
EFFECTS = [
    "Static", "Breathing", "Chase / Spin", "Rainbow", "Strobe",
    "Fire", "Theater Chase", "Sparkle", "Color Cycle",
]

# key -> (name, unit, device_class, state_class, icon, entity_category)
SENSORS: Dict[str, tuple] = {
    "temperature":  ("Temperature", "°C", "temperature", "measurement", None, None),
    "humidity":     ("Humidity", "%", "humidity", "measurement", None, None),
    "eco2":         ("eCO2", "ppm", "carbon_dioxide", "measurement", None, None),
    "tvoc":         ("TVOC", "ppb", "volatile_organic_compounds_parts",
                     "measurement", None, None),
    "aqi":          ("Air Quality Index", None, None, "measurement",
                     "mdi:air-filter", None),
    "pm1_0":        ("PM1.0", "µg/m³", "pm1", "measurement", None, None),
    "pm2_5":        ("PM2.5", "µg/m³", "pm25", "measurement", None, None),
    "pm10":         ("PM10", "µg/m³", "pm10", "measurement", None, None),
    "pressure_hpa": ("Pressure", "hPa", "atmospheric_pressure",
                     "measurement", None, None),
    "chip_temp":    ("CPU Temperature", "°C", "temperature", "measurement",
                     None, "diagnostic"),
    "uptime_s":     ("Uptime", "s", "duration", "total_increasing", None,
                     "diagnostic"),
}


class MqttBridge:
    def __init__(self, loop: asyncio.AbstractEventLoop) -> None:
        self._loop = loop
        self._connected = False
        self.base = f"hexair/{config.device_id}"
        self.state_topic = f"{self.base}/state"
        self.avail_topic = f"{self.base}/availability"
        self.light_state_topic = f"{self.base}/ring/state"
        self.light_cmd_topic = f"{self.base}/ring/set"

        self._client = mqtt.Client(
            mqtt.CallbackAPIVersion.VERSION2,
            client_id=f"hexair-{config.device_id}",
        )
        self._client.username_pw_set(config.mqtt_user, config.mqtt_password)
        # Last will: if this service dies, HA marks the entities unavailable
        # rather than showing the last value forever.
        self._client.will_set(self.avail_topic, "offline", retain=True)
        self._client.on_connect = self._on_connect
        self._client.on_disconnect = self._on_disconnect
        self._client.on_message = self._on_message

    # --- lifecycle ---------------------------------------------------------

    def start(self) -> None:
        log.info("connecting to mqtt %s:%s as %s",
                 config.mqtt_host, config.mqtt_port, config.mqtt_user)
        self._client.connect_async(config.mqtt_host, config.mqtt_port,
                                   keepalive=60)
        self._client.loop_start()

    def stop(self) -> None:
        try:
            self._client.publish(self.avail_topic, "offline", retain=True)
            self._client.loop_stop()
            self._client.disconnect()
        except Exception as e:  # noqa: BLE001 - shutdown is best-effort
            log.warning("mqtt shutdown: %s", e)

    @property
    def connected(self) -> bool:
        return self._connected

    # --- callbacks (paho thread) ------------------------------------------

    def _on_connect(self, client, userdata, flags, reason_code, properties=None):
        if reason_code != 0:
            log.error("mqtt connect failed: %s", reason_code)
            return
        self._connected = True
        log.info("mqtt connected")
        self._publish_discovery()
        client.publish(self.avail_topic, "online", retain=True)
        client.subscribe(self.light_cmd_topic, qos=1)

    def _on_disconnect(self, client, userdata, flags, reason_code,
                       properties=None):
        self._connected = False
        log.warning("mqtt disconnected: %s (paho will retry)", reason_code)

    def _on_message(self, client, userdata, msg):
        if msg.topic != self.light_cmd_topic:
            return
        try:
            payload = json.loads(msg.payload.decode())
        except Exception as e:  # noqa: BLE001 - payload is external input
            log.warning("bad light command: %s", e)
            return
        # Hop back onto the event loop; this runs on paho's network thread.
        asyncio.run_coroutine_threadsafe(
            self._handle_light_command(payload), self._loop
        )

    # --- discovery ---------------------------------------------------------

    def _device_block(self) -> Dict[str, Any]:
        return {
            "identifiers": [config.device_id],
            "name": "Hexair",
            "manufacturer": "Promethium",
            "model": "Hexair ESP32 Air Quality Monitor",
        }

    def _publish_discovery(self) -> None:
        prefix = config.mqtt_discovery_prefix
        dev = self._device_block()
        count = 0

        for key, (name, unit, dclass, sclass, icon, ecat) in SENSORS.items():
            cfg: Dict[str, Any] = {
                "name": name,
                "unique_id": f"{config.device_id}_{key}",
                "state_topic": self.state_topic,
                # One shared state topic for every sensor: the device publishes
                # a single JSON blob and each entity extracts its own field.
                "value_template": "{{ value_json.%s }}" % key,
                "availability_topic": self.avail_topic,
                "device": dev,
            }
            if unit:
                cfg["unit_of_measurement"] = unit
            if dclass:
                cfg["device_class"] = dclass
            if sclass:
                cfg["state_class"] = sclass
            if icon:
                cfg["icon"] = icon
            if ecat:
                cfg["entity_category"] = ecat
            self._client.publish(
                f"{prefix}/sensor/{config.device_id}/{key}/config",
                json.dumps(cfg), retain=True,
            )
            count += 1

        # Sensor warm-up state as a binary sensor.
        self._client.publish(
            f"{prefix}/binary_sensor/{config.device_id}/ready/config",
            json.dumps({
                "name": "Sensors Ready",
                "unique_id": f"{config.device_id}_ready",
                "state_topic": self.state_topic,
                "value_template": "{{ 'ON' if value_json.ready else 'OFF' }}",
                "availability_topic": self.avail_topic,
                "entity_category": "diagnostic",
                "device": dev,
            }), retain=True,
        )
        count += 1

        # NeoPixel ring as a JSON-schema light.
        self._client.publish(
            f"{prefix}/light/{config.device_id}/ring/config",
            json.dumps({
                "name": "NeoPixel Ring",
                "unique_id": f"{config.device_id}_ring",
                "schema": "json",
                "state_topic": self.light_state_topic,
                "command_topic": self.light_cmd_topic,
                "availability_topic": self.avail_topic,
                "brightness": True,
                "supported_color_modes": ["rgb"],
                "effect": True,
                "effect_list": EFFECTS,
                "device": dev,
            }), retain=True,
        )
        count += 1

        log.info("published discovery for %d entities under %s/", count, prefix)

    # --- publishing --------------------------------------------------------

    def publish_state(self, values: Dict[str, Any]) -> None:
        """Publish one sensor reading. Safe to call when disconnected."""
        if not self._connected:
            return
        self._client.publish(self.state_topic, json.dumps(values), retain=False)

    def publish_light_state(self, neo: Dict[str, Any]) -> None:
        """Mirror the device's NeoPixel state into HA."""
        if not self._connected:
            return
        effect_idx = int(neo.get("effect") or 0)
        payload = {
            "state": "ON" if neo.get("enabled") else "OFF",
            "brightness": int(neo.get("brightness") or 0),
            "color_mode": "rgb",
            "color": {
                "r": int(neo.get("r") or 0),
                "g": int(neo.get("g") or 0),
                "b": int(neo.get("b") or 0),
            },
            "effect": EFFECTS[effect_idx] if 0 <= effect_idx < len(EFFECTS)
            else EFFECTS[0],
        }
        self._client.publish(self.light_state_topic, json.dumps(payload),
                             retain=True)

    # --- light command handling (event loop) -------------------------------

    async def _handle_light_command(self, payload: Dict[str, Any]) -> None:
        patch: Dict[str, Any] = {}

        if "state" in payload:
            patch["enabled"] = payload["state"] == "ON"
        if "brightness" in payload:
            patch["brightness"] = max(0, min(255, int(payload["brightness"])))

        colour = payload.get("color")
        if isinstance(colour, dict):
            patch["r"] = max(0, min(255, int(colour.get("r", 0))))
            patch["g"] = max(0, min(255, int(colour.get("g", 0))))
            patch["b"] = max(0, min(255, int(colour.get("b", 0))))

        if "effect" in payload and payload["effect"] in EFFECTS:
            patch["effect"] = EFFECTS.index(payload["effect"])

        # Colour and effect only take hold in manual mode; without this, the
        # sensor-driven animation would immediately overwrite whatever HA set.
        if any(k in patch for k in ("r", "g", "b", "effect")):
            patch["manual"] = True

        if not patch:
            return

        try:
            headers = {"Content-Type": "application/json"}
            if config.device_token:
                headers["Authorization"] = f"Bearer {config.device_token}"
            async with httpx.AsyncClient(timeout=config.device_timeout_s) as c:
                res = await c.post(config.neo_url(), headers=headers,
                                   json=patch)
                res.raise_for_status()
            log.info("light command applied: %s", patch)
        except Exception as e:  # noqa: BLE001 - device may be offline
            log.warning("light command failed: %s", e)
            return

        # Echo back immediately so HA's UI doesn't sit in a pending state
        # until the next poll picks the change up.
        await self.refresh_light_state()

    async def refresh_light_state(self) -> None:
        """Read /neo and mirror it, so changes made in the app or web UI show
        up in Home Assistant too."""
        try:
            headers = {}
            if config.device_token:
                headers["Authorization"] = f"Bearer {config.device_token}"
            async with httpx.AsyncClient(timeout=config.device_timeout_s) as c:
                res = await c.get(config.neo_url(), headers=headers)
                res.raise_for_status()
                self.publish_light_state(res.json())
        except Exception as e:  # noqa: BLE001 - device may be offline
            log.debug("neo state refresh failed: %s", e)


bridge: Optional[MqttBridge] = None
