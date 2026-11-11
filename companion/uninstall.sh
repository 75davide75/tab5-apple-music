#!/bin/sh
# Stops and removes the companion.
LABEL=com.tab5music.companion
launchctl bootout "gui/$(id -u)/$LABEL" 2>/dev/null || true
rm -f "$HOME/Library/LaunchAgents/$LABEL.plist"
rm -rf "$HOME/Library/Application Support/tab5-companion"
echo "Removed."
