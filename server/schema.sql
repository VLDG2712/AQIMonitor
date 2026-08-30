-- Hexair readings table (MariaDB 12.x).
--
-- Applied automatically at service startup by db.ensure_schema(), against the
-- database named in HEXAIR_DB_NAME. Idempotent, so it runs on every boot.
-- The database and user themselves are created once by grant.sql, which needs
-- MariaDB root.
--
-- All timestamps are stored in UTC. The service pins its session with
-- `SET time_zone = '+00:00'` so UNIX_TIMESTAMP() bucketing is unambiguous
-- regardless of the container's or server's local timezone.

CREATE TABLE IF NOT EXISTS readings (
  device_id    VARCHAR(32)       NOT NULL DEFAULT 'hexair-1',
  ts           DATETIME(3)       NOT NULL,

  -- AHT21
  temperature  FLOAT             NULL,
  humidity     FLOAT             NULL,

  -- ENS160
  eco2         SMALLINT UNSIGNED NULL,
  tvoc         SMALLINT UNSIGNED NULL,
  aqi          TINYINT  UNSIGNED NULL,

  -- BMP580
  pressure_hpa FLOAT             NULL,
  altitude_m   FLOAT             NULL,
  bmp_temp     FLOAT             NULL,

  -- PMS5003
  pm1_0        SMALLINT UNSIGNED NULL,
  pm2_5        SMALLINT UNSIGNED NULL,
  pm10         SMALLINT UNSIGNED NULL,

  -- Device health
  chip_temp    FLOAT             NULL,
  ready        BOOLEAN           NOT NULL DEFAULT 0,
  uptime_s     INT UNSIGNED      NULL,

  -- device_id leads so a second Hexair can be added without a migration,
  -- and range scans for one device stay contiguous on disk.
  PRIMARY KEY (device_id, ts)
) ENGINE=InnoDB;
