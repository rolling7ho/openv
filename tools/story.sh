#!/bin/zsh
# Focus GTA, wait for the start screen, select STORY and press Enter. Keys are only sent while GTA is frontmost.
SP=${0:A:h}
front() { osascript -e 'tell application "System Events" to get name of first process whose frontmost is true'; }
focus() { osascript -e "tell application \"System Events\" to set frontmost of (first process whose unix id is $(pgrep -f GTA5_Enhanced | head -1)) to true"; sleep 1; }
key() { focus; [ "$(front)" = wine ] && swift $SP/keys.swift $1 none || echo "skip key $1 (not focused)"; }
sleep ${WAIT:-35}
key 14; sleep 1.5; key 14; sleep 2; key 36
