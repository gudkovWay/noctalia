#!/bin/sh
set -eu

if [ "$#" -ne 1 ] || [ "$1" = --help ]; then
    printf '%s\n' 'Usage: PREFIX=$HOME/.local integrations/agent-workspaces/install.sh <built-agent-workspace-toplevels>' \
        'Build only the helper: meson compile -C <configured-build-dir> agent-workspace-toplevels' \
        'Installs standalone gallery/helper and recovery sources. Never edits or reloads Hyprland/OMP config.'
    [ "$#" -eq 1 ] && [ "$1" = --help ] && exit 0
    exit 2
fi

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
prefix=${PREFIX:-"$HOME/.local"}
helper=$1
if [ ! -f "$helper" ] || [ ! -x "$helper" ]; then
    printf '%s\n' 'Expected an existing executable agent-workspace-toplevels build.' >&2
    exit 2
fi
for file in agent-app agent-workspace.js agent_workspace.lua silent.lua hypr-agent-guard gallery.lua; do
    test -f "$root/integrations/agent-workspaces/$file"
done
test -f "$root/tools/agent_workspace_gallery.py"

install -d "$prefix/bin" "$prefix/libexec" "$prefix/share/noctalia/agent-workspaces"
install -m 755 "$root/tools/agent_workspace_gallery.py" "$prefix/bin/agent-workspace-gallery"
install -m 755 "$helper" "$prefix/libexec/agent-workspace-toplevels"
for file in agent-app agent-workspace.js agent_workspace.lua silent.lua hypr-agent-guard gallery.lua; do
    install -m 644 "$root/integrations/agent-workspaces/$file" "$prefix/share/noctalia/agent-workspaces/$file"
done
printf '%s\n' "Gallery installed under $prefix; recovery sources: $prefix/share/noctalia/agent-workspaces" \
    'Routing and keybindings are preserved as source files only; existing live configuration is untouched.'
