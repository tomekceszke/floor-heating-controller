# hc-data: floor-heating-controller backend

Database `floorheating` and the `fh-ingest` service on hc-data (192.168.11.16), on the shared layer from `../hc-data`.

```
board .247 ── floorheating/<mac>/{state,status} ── Mosquitto ── fh-ingest ── PostgreSQL floorheating
```

| Unit | What |
|---|---|
| `fh-ingest.service` | MQTT → PostgreSQL (`ingest/fh_ingest.py`): one `sample` row per state document (60 s, and at once on a pump change), stamped on receipt; retained replays are not stored |
| `hcdata-pg-backup@floorheating.timer` | nightly `pg_dump`, 14 days (hc-data template) |

| MQTT user | Access |
|---|---|
| `fh-ingest` | read `floorheating/#` (the board's own user is in the homekit fragment) |

| PG role | Access |
|---|---|
| `fh_ingest` | peer; insert samples, update device status |
| `fh_read` | password from the LAN; SELECT |

Secrets on the host only: `/etc/fh-server/secrets.env` (root 0600).

```sh
cp server/secrets.env.example server/secrets.env    # fill
server/deploy.sh                                     # /opt/fh-server, /etc/fh-server, install.sh
ssh root@192.168.11.16 journalctl -u fh-ingest -f
ssh root@192.168.11.16 'runuser -u postgres -- psql floorheating -c "select * from sample order by ts desc limit 5"'
uv run --python 3.12 --with pgserver --with 'psycopg[binary]' --with paho-mqtt --with ../hc-data/python \
    python -m unittest server/ingest/test_fh_ingest.py
```
