#!/usr/bin/env bash
# Idempotent provisioning of the floor-heating-controller part of hc-data, on the shared layer from ../hc-data.
# Run as root from /opt/fh-server (deploy.sh does this). Mosquitto and PostgreSQL are only reloaded, so the
# other projects' devices and ingest services keep running.
set -euo pipefail
cd "$(dirname "$0")"

SECRETS=/etc/fh-server/secrets.env
[ -f "$SECRETS" ] || { echo "$SECRETS missing" >&2; exit 1; }
set -a; . "$SECRETS"; set +a
: "${MQTT_INGEST_PASS:?}" "${PG_READ_PASS:?}"

. /opt/hc-data/base/lib/provision.sh
hc_require_version 0.1.2

echo "== mosquitto"
hc_mqtt_project floorheating mosquitto/floorheating.acl fh-ingest "$MQTT_INGEST_PASS"

echo "== postgresql"
hc_pg_role fh_ingest
hc_pg_role fh_read "$PG_READ_PASS"
hc_pg_db floorheating
hc_pg_hba_lan floorheating fh_read
hc_psql -d floorheating -f db/schema.sql

echo "== service"
hc_system_user fh_ingest
chmod 600 "$SECRETS"
hc_venv venv
install -m 644 ingest/fh-ingest.service /etc/systemd/system/
systemctl daemon-reload
hc_pg_backup floorheating
systemctl enable -q fh-ingest
systemctl restart fh-ingest

echo "== status"
systemctl is-active mosquitto postgresql fh-ingest hcdata-pg-backup@floorheating.timer
for unit in hc-ingest wc-ingest mc-ingest ac-ingest; do
    systemctl is-active -q "$unit" 2>/dev/null && echo "($unit still running)" || echo "WARNING: $unit not active"
done
