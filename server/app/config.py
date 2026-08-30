"""Environment-driven configuration."""
import os


def _int(name: str, default: int) -> int:
    raw = os.getenv(name)
    if raw is None or raw.strip() == "":
        return default
    return int(raw)


class Config:
    # --- Device being polled ---
    device_url = os.getenv("HEXAIR_DEVICE_URL", "http://192.168.2.220:9091/air")
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

    @classmethod
    def validate(cls) -> None:
        missing = []
        if not cls.db_password:
            missing.append("HEXAIR_DB_PASSWORD")
        if not cls.api_token:
            missing.append("HEXAIR_API_TOKEN")
        if missing:
            raise RuntimeError(
                "Missing required environment variables: " + ", ".join(missing)
            )


config = Config()
