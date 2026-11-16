#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Leo Kuroshita (@kurogedelic)
# Builds maiden (monome's web editor, GPL-3.0) for Uzume into build/maiden/:
#   maiden      the Go server (arm64), built in the container uzume-nb (Debian 13 golang-go)
#   app/        the web UI, built in a node:16 container (maiden-web)
# One change to maiden: a --host flag, so that Uzume can serve it on 127.0.0.1 only.
# Usage: sh packaging/maiden/build-maiden.sh    (sources in third_party/maiden)
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
SRC=$ROOT/third_party/maiden
OUT=$ROOT/build/maiden
mkdir -p "$OUT"

# server
docker exec uzume-nb sh -c 'command -v go >/dev/null || (apt-get update -qq && apt-get install -y -qq golang-go >/dev/null)'
tar -C "$SRC" --exclude=.git --exclude=web -cf - . | docker exec -i uzume-nb sh -c 'rm -rf /src/maiden && mkdir -p /src/maiden && tar -C /src/maiden -xf -'
docker exec -w /src/maiden uzume-nb sh -c '
  sed -i "s/^\thttpFD   int$/\thttpFD   int\n\thttpHost string/" cmd/server.go
  sed -i "s/^\tserverCmd.Flags().IntVar(&httpPort, \"port\", 5000, \"HTTP \`port\`\")$/&\n\tserverCmd.Flags().StringVar(\&httpHost, \"host\", \"\", \"`address` to listen on (default: all)\")/" cmd/server.go
  sed -i "s/net.Listen(\"tcp\", fmt.Sprintf(\":%d\", httpPort))/net.Listen(\"tcp\", fmt.Sprintf(\"%s:%d\", httpHost, httpPort))/" cmd/server.go
  grep -c httpHost cmd/server.go | grep -qx 3 || { echo "maiden patch did not apply"; exit 1; }
  CGO_ENABLED=0 go build -buildvcs=false -trimpath -ldflags "-s -w -X github.com/monome/maiden/cmd.version=2e64f7d" -o maiden .'
docker cp uzume-nb:/src/maiden/maiden "$OUT/maiden"

# web UI
if [ ! -f "$OUT/app/index.html" ]; then
  docker inspect maiden-web >/dev/null 2>&1 || docker run -d --name maiden-web --platform linux/arm64 node:16-bullseye-slim sleep infinity >/dev/null
  tar -C "$SRC" -cf - web | docker exec -i maiden-web sh -c 'rm -rf /src/web && mkdir -p /src && tar -C /src -xf -'
  docker exec -w /src/web maiden-web sh -c "yarn install --frozen-lockfile --production --network-timeout 600000 >/dev/null &&
    printf \"export const VERSION = '2e64f7d';\nexport const COMMIT = '2e64f7d';\n\" > src/version.js &&
    NODE_OPTIONS=--max-old-space-size=2048 npx react-scripts build >/dev/null"
  rm -rf "$OUT/app"; docker cp maiden-web:/src/web/build "$OUT/app"
fi
cp "$SRC/maiden.yaml" "$OUT/maiden.yaml"

# license texts of everything built into the maiden binary (Go modules) and the web bundle (npm packages)
L=$OUT/maiden-third-party-licenses.txt
{
  echo "maiden is GPL-3.0 (https://github.com/monome/maiden). It contains the following third-party code."
  echo
  echo "Part 1: Go modules compiled into the maiden program"
  echo
  docker exec -w /src/maiden uzume-nb sh -c '
    for f in $(ls /usr/share/doc/golang-1.*-go/copyright | head -1); do echo "=============================================================================="; echo "Go standard library $(go env GOVERSION)"; echo "=============================================================================="; cat $f; echo; done
    go version -m maiden | awk "/^\tdep/{print \$2\" \"\$3}" | grep -v "^github.com/monome/maiden" | while read m v; do
      d=$(go env GOMODCACHE)/$(echo "$m@$v" | sed "s/[A-Z]/!\L&/g"); f=$(ls $d/LICENSE* $d/LICENCE* $d/COPYING* 2>/dev/null | head -1)
      echo "=============================================================================="; echo "$m $v"; echo "=============================================================================="
      cat "$f"; echo
    done'
  echo "Part 2: npm packages in the web app (app/static/js/main.*.js)"
  echo
  docker cp "$ROOT/packaging/maiden/jslicenses.js" maiden-web:/tmp/jslicenses.js
  docker exec maiden-web node /tmp/jslicenses.js /src/web
} > "$L"
rm -f "$OUT"/app/static/js/*.map "$OUT"/app/static/css/*.map   # source maps are not needed on the device
ls -l "$OUT"
