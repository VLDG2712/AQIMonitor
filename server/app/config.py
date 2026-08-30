"""Environment-driven configuration."""
import os


def _int(name: str, default: int) -> int:
    raw = os.getenv(name)
    if raw is None or raw.strip() == "":
        return default
    return int(raw)


class Config:
    # --- Device being polled ---
    device_url = os.getenv("HEXAIR_DEVICE_URL", "http://192.168.2.116:9091/air")
    device_id = os.getenv("HEXAIR_DEVICE_ID", "hexair-1")
    # Sent as `Authorization: Bearer ...` when set. /air is currently
    # unauthenticated in the firmware; setting this is harmless until it isn't.
    device_token = os.getenv("HEXAIR_DEVICE_TOKEN", "")
    poll_interval_s = _int("HEXAIR_POLL_INTERVAL", 30)
    device_timeout_s = _int("HEXAIR_DEVICE_TIMEOUT", 5)

    # --- Database ---
    db_host = os.getenv("HEXAIR_DB_HOST", "mariadb")
    db_port = _int("HEXAIR_DB_PORT", 3306)
    db_name = os.getenv("HEXAIR_DB_NAME", "hexair")
    db_user = os.getenv("HEXAIR_DB_USER", "hexair")
    db_password = os.getenv("HEXAIR_DB_PASSWORD", "")

    # --- Read API ---
    # Bearer token the phone must present. Required: refuse to start without it.
    api_token = os.getenv("HEXAIR_API_TOKEN", "")
    # Cap on points returned to a client, before bucketing is chosen.
    max_points = _int("HEXAIR_MAX_POINTS", 600)

    # --- MQTT / Home Assistant ---
    mqtt_enabled = os.getenv("HEXAIR_MQTT_ENABLED", "false").lower() == "true"
    # Mosquitto runs on the Pi host, not in Docker, so from inside the
    # container it is reached through the host gateway (see docker-compose.yml).
    mqtt_host = os.getenv("HEXAIR_MQTT_HOST", "host.docker.internal")
    mqtt_port = _int("HEXAIR_MQTT_PORT", 1883)
    mqtt_user = os.getenv("HEXAIR_MQTT_USER", "hexair")
    mqtt_password = os.getenv("HEXAIR_MQTT_PASSWORD", "")
    # Must match the MQTT integration's discovery prefix in Home Assistant.
    mqtt_discovery_prefix = os.getenv("HEXAIR_MQTT_DISCOVERY_PREFIX",
                                      "homeassistant")

    @classmethod
    def neo_url(cls) -> str:
        """Control endpoint for the NeoPixel ring.

        Derived from device_url so a single address change moves both, but
        overridable if the two ever diverge.
        """
        override = os.getenv("HEXAIR_DEVICE_NEO_URL", "")
        if override:
            return override
        base = cls.device_url
        return base[: -len("/air")] + "/neo" if base.endswith("/air") else base

    @classmethod
    def validate(cls) -> None:
        missing = []
        if not cls.db_password:
            missing.append("HEXAIR_DB_PASSWORD")
        if not cls.api_token:
            missing.append("HEXAIR_API_TOKEN")
        if cls.mqtt_enabled and not cls.mqtt_password:
            missing.append("HEXAIR_MQTT_PASSWORD (required when MQTT enabled)")
        if missing:
            raise RuntimeError(
                "Missing required environment variables: " + ", ".join(missing)
            )


config = Config()
