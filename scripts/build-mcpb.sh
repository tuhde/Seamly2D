#!/bin/sh
# Packs dist/claude-desktop into build/seamly2d.mcpb, a bundle Claude Desktop installs by drag and drop.
set -e
root=$(cd "$(dirname "$0")/.." && pwd)
stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
cp -r "$root/dist/claude-desktop/." "$stage/"
cp "$root/dist/seamly2d.png" "$stage/icon.png"
mkdir -p "$root/build"
rm -f "$root/build/seamly2d.mcpb"
(cd "$stage" && zip -qr "$root/build/seamly2d.mcpb" manifest.json icon.png server)
echo "$root/build/seamly2d.mcpb"
