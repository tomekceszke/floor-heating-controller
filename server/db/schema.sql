-- floor-heating-controller schema (PostgreSQL, database "floorheating"). Idempotent: safe to re-apply.
-- sample = one state document from floorheating/<mac>/state: every 60 s and at once on a pump change.
-- Roles fh_ingest and fh_read are created by install.sh before this file runs.

CREATE TABLE IF NOT EXISTS sample (
    device          text NOT NULL CHECK (device ~ '^[0-9a-f]{12}$'),
    ts              timestamptz NOT NULL,              -- receipt time on hc-data (the document carries none)
    temp_c          real,                              -- DS18B20 on the copper supply before the injection valve
    temp_age_s      integer,
    pump_on         boolean NOT NULL,
    reason          text NOT NULL,                     -- control decision: auto_cold, auto_warm, overheat, ...
    manual          text NOT NULL,
    maintenance     boolean NOT NULL,
    overheat        boolean NOT NULL,
    sensor_failed   boolean NOT NULL,
    start_c         real,
    stop_c          real,
    critical_c      real,
    runtime_today_s integer,
    starts_today    integer,
    PRIMARY KEY (device, ts)
);

CREATE TABLE IF NOT EXISTS device_status (
    device         text PRIMARY KEY,
    online         boolean,
    status_ts      timestamptz,                        -- when floorheating/<mac>/status last arrived
    last_sample_ts timestamptz
);

GRANT SELECT, INSERT ON sample TO fh_ingest;
GRANT SELECT, INSERT, UPDATE ON device_status TO fh_ingest;
GRANT SELECT ON sample, device_status TO fh_read;
