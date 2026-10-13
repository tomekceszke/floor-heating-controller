#!/usr/bin/env bash
# Deploys server/ to hc-data and provisions the floor-heating-controller part. Usage: server/deploy.sh [root@host]
# Needs the shared layer first (../../hc-data/deploy.sh).
set -euo pipefail
HOST=${1:-root@192.168.11.16}
cd "$(dirname "$0")"
[ -f secrets.env ] || { echo "server/secrets.env missing (see secrets.env.example)" >&2; exit 1; }
ssh "$HOST" 'test -f /opt/hc-data/VERSION' || { echo "hc-data not installed on $HOST: run hc-data/deploy.sh" >&2; exit 1; }

# Replace everything under /opt/fh-server except the venv
ssh "$HOST" 'install -d -m 755 /opt/fh-server && install -d -m 700 /etc/fh-server &&
             find /opt/fh-server -mindepth 1 -maxdepth 1 ! -name venv -exec rm -rf {} +'
COPYFILE_DISABLE=1 tar --no-xattrs -czf - --exclude secrets.env --exclude __pycache__ --exclude 'test_*' . |
    ssh "$HOST" 'tar -xzf - --no-same-owner -C /opt/fh-server'
ssh "$HOST" 'umask 077 && cat > /etc/fh-server/secrets.env' < secrets.env
ssh "$HOST" 'bash /opt/fh-server/install.sh'
