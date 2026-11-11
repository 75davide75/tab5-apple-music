#!/bin/sh
# Builds the companion and installs it as a login item (LaunchAgent) for the current user.
set -e
cd "$(dirname "$0")"
LABEL=com.tab5music.companion
DEST="$HOME/Library/Application Support/tab5-companion"
PLIST="$HOME/Library/LaunchAgents/$LABEL.plist"
LOG="$HOME/Library/Logs/tab5-companion.log"

swift build -c release
mkdir -p "$DEST" "$HOME/Library/LaunchAgents"
launchctl bootout "gui/$(id -u)/$LABEL" 2>/dev/null || true
cp .build/release/tab5-companion "$DEST/tab5-companion"
codesign --force --sign - --identifier "$LABEL" "$DEST/tab5-companion"
sed -e "s|__BINARY__|$DEST/tab5-companion|" -e "s|__LOG__|$LOG|" "launchd/$LABEL.plist" > "$PLIST"
launchctl bootstrap "gui/$(id -u)" "$PLIST"
echo "Installed. Log: $LOG"
