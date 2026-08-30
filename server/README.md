# Hexair History Service

Collects readings from the Hexair ESP32 into MariaDB on the Pi, and serves them
back to the Flutter app over a small authenticated HTTP API.

## Why pull, not push

The Pi polls the ESP32's `/air` endpoint rather than the ESP32 pushing to the Pi:

- The firmware needs no changes at all.
- The poll loop lives on the always-on machine, where it can be restarted and
  its logs read, instead of on the microcontroller.
- `/air` already rate-limits to 1 req/sec, which comfortably accommodates the
  default 30s interval.
- Push would additionally require a LittleFS ring buffer *and* RTC time sync on
  the ESP32 to survive WiFi drops — significant firmware complexity. Note that
  `/air` returns no timestamp of its own, so the collector stamps rows either way.

The trade-off: readings are lost for any window in which the **Pi** is down.
That is the better failure mode here, since the Pi is the always-on box.

## Layout

| Path | Purpose |
|---|---|
| `app/config.py` | Environment-driven settings |
| `app/db.py` | MariaDB access, bucket selection, aggregation SQL |
| `app/collector.py` | Poll loop: `/air` → row |
| `app/api.py` | Read API + bearer auth |
| `app/main.py` | FastAPI app; runs the collector as a background task |
| `schema.sql` | Table definition, applied automatically at startup |

## Deployment

Lives at `/home/dietpi/hexair` on the Pi (`192.168.2.54`), published on port
**9101** bound to `0.0.0.0` so it is reachable both on the LAN and over
Tailscale.

It connects to the **existing** `mariadb` container. That container runs on
Docker's default bridge, which has no container-name DNS, so rather than
re-networking a container Home Assistant depends on, this service reaches
MariaDB through its already-published port via `host.docker.internal`
(mapped to the host gateway in `docker-compose.yml`).

One-time bootstrap, which needs MariaDB root. This creates only the database and
the `hexair` user — the service creates its own table on startup, so
provisioning cannot end up half-done:

```bash
docker exec -i mariadb sh -c 'exec mariadb -uroot -p$MYSQL_ROOT_PASSWORD' \
  < /home/dietpi/hexair/grant.sql
```

Then:

```bash
cd /home/dietpi/hexair && docker compose up -d
```

Secrets live in `.env` (mode 600, gitignored), generated with `openssl rand`.

## API

All endpoints except `/health` require `Authorization: Bearer <HEXAIR_API_TOKEN>`.

### `GET /readings?from=<ms>&to=<ms>&bucket=auto`

`from`/`to` are epoch milliseconds. `bucket` is `auto` (default), `raw`, or a
width in seconds.

**Why bucketing matters:** at 30s intervals a year holds ~1.05M rows. `auto`
picks the smallest granularity from the ladder (30s → 1m → 5m → 15m → 30m → 1h →
3h → 6h → 1d → 1w) that keeps the response under `HEXAIR_MAX_POINTS` (600), so
any range — an hour or a year — returns a few hundred points. Averages are
computed in SQL; integer columns are `ROUND()`ed, and `ready` is `MAX()` so a
bucket counts as ready if the sensor was warmed up at any point within it.

Responses use the **nested** shape (`ens160`/`aht21`/`pms5003`/`bmp580`), not
`/air`'s flat one. That is deliberate: the Flutter model's nested branch is the
only one that honours an incoming `timestamp`, which is precisely what history
needs. Null aggregates are emitted as `0`, because the client casts with a
non-nullable `as num`.

### `GET /stats`

Row count and coverage window. Backs the app's "Test Connection" button.

### `GET /health`

Unauthenticated. Reports database reachability and collector freshness
(`device_ok` goes false after roughly three missed polls). Suitable as an
Uptime Kuma target.

## Home Assistant (MQTT)

Set `HEXAIR_MQTT_ENABLED=true` and the service publishes MQTT discovery
configs, so a **Hexair** device appears in Home Assistant with 13 entities and
no YAML.

Mosquitto runs on the Pi host rather than in Docker, so the service reaches it
through `host.docker.internal` (the same host-gateway mapping used for
MariaDB). Home Assistant itself runs with `network=host`, so *its* broker
address is plain `localhost`.

The broker requires authentication (`password_file`, no `allow_anonymous`).
Two dedicated accounts are used rather than sharing one: `hexair` for this
service and `homeassistant` for HA. Both are created by root-only helper
scripts that read their password from a mode-600 file, so no secret is ever
typed or echoed:

```bash
sudo bash /home/dietpi/hexair/mqtt_user.sh      # creates 'hexair'
sudo bash /home/dietpi/hexair/ha_mqtt_user.sh   # creates 'homeassistant'
```

### Topics

| Topic | Purpose |
|---|---|
| `hexair/<id>/state` | All sensor values, one JSON blob per poll |
| `hexair/<id>/availability` | `online`/`offline`, with an MQTT last-will |
| `hexair/<id>/ring/state` | Current NeoPixel state (retained) |
| `hexair/<id>/ring/set` | Light commands from HA |

**All 13 entities share one state topic.** The service publishes a single JSON
document per poll and each entity pulls its own field with a `value_template`
— one message every 30s instead of thirteen.

Discovery configs are published **retained**, so HA picks them up the moment it
subscribes rather than waiting for the next reading. The availability topic
carries a **last will**, so if this service dies HA marks the entities
unavailable instead of showing stale values forever.

### The light entity

The ring is exposed as a JSON-schema light with brightness, RGB, and all nine
firmware effects. `EFFECTS` in `app/mqtt.py` is ordered to match the firmware's
`FX_*` constants **by index** — reordering that list silently remaps effects.

Setting colour or effect from HA also forces `manual: true`. Those settings
only take hold when the firmware isn't running its sensor-driven animation;
without it, an automation would set a colour and have the AQI breathing effect
immediately overwrite it.

`/neo` is re-read on every poll, so changes made from the Flutter app or the
device's own web UI are reflected back into HA. Note `/neo` is
auth-protected (unlike `/air`), so `HEXAIR_DEVICE_TOKEN` must be set for light
control to work — update it whenever the ESP32 token is rotated.

## Storage

~2,880 rows/day, ~1.05M/year, roughly **100 MB/year** including indexes.
Retention is unlimited by design — nothing prunes.

`PRIMARY KEY (device_id, ts)` leads with `device_id` so a second Hexair can be
added without a migration, and so range scans for one device stay contiguous.

All timestamps are stored **UTC**; the service pins `SET time_zone = '+00:00'`
so `UNIX_TIMESTAMP()` bucketing does not depend on anyone's local timezone.

## Operations

```bash
docker compose logs -f hexair     # follow
docker compose restart hexair     # after an .env change
docker compose up -d --build      # after a code change
curl -s localhost:9101/health | jq
```
