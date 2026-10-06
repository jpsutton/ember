#!/bin/sh
# Runs a throwaway Jellyfin server for development, with the fake library
# from dev-media.sh, and sets it up on first start.
#
# Usage: scripts/dev-server.sh [start|stop|logs]
# Server: http://<this host>:8096, admin user "ember" / password "ember",
# a second user "guest" / "guest". Data lives in $EMBER_DEV_DIR
# (default ~/.cache/ember-dev).
set -eu

here=$(cd "$(dirname "$0")" && pwd)
dir=${EMBER_DEV_DIR:-$HOME/.cache/ember-dev}
name=ember-jellyfin
image=${EMBER_JELLYFIN_IMAGE:-jellyfin/jellyfin:latest}

case ${1:-start} in
  stop)
    docker rm -f "$name" >/dev/null
    exit 0
    ;;
  logs)
    exec docker logs -f "$name"
    ;;
  start) ;;
  *)
    echo "usage: $0 [start|stop|logs]" >&2
    exit 2
    ;;
esac

"$here/dev-media.sh" "$dir/media"
mkdir -p "$dir/jellyfin-config" "$dir/jellyfin-cache"

if ! docker inspect "$name" >/dev/null 2>&1; then
  # Host networking so LAN discovery (UDP 7359 broadcasts) reaches it.
  docker run -d --name "$name" --network host --user "$(id -u):$(id -g)" \
    -v "$dir/jellyfin-config:/config" -v "$dir/jellyfin-cache:/cache" -v "$dir/media:/media:ro" \
    "$image" >/dev/null
else
  docker start "$name" >/dev/null
fi

python3 "$here/dev-server-setup.py" http://localhost:8096
