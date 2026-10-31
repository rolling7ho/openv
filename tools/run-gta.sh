#!/bin/zsh
# usage: gtarun.sh  — restart bottle, launch GTA V via Steam, report game window title
B=~/Library/Containers/com.rolling7ho.Vodka/Bottles/38C59927-8B51-4C81-B169-2C5E1FFE8974; W=~/Library/Application\ Support/com.rolling7ho.Vodka/Libraries/Wine/bin; SP=${0:A:h}
export WINEPREFIX="$B" WINEMSYNC=1 WINEDEBUG=${GTADEBUG:--all}; "$W/wineserver" -k; sleep 3
S="$B/drive_c/Program Files (x86)/Steam/steam.exe"; cd "$(dirname "$S")"; ("$W/wine" start /unix "$S" -cef-force-gpu -applaunch 3240220 > ${GTALOG:-/dev/null} 2>&1 &)
for i in $(seq 1 150); do pgrep -f GTA5_Enhanced >/dev/null && break; sleep 2; done
[ -z "$(pgrep -f GTA5_Enhanced)" ] && { echo "no game process"; exit; }
for i in $(seq 1 60); do w=$(swift $SP/winsof.swift $(pgrep -f 'GTA5_Enhanced') 2>/dev/null); [ -n "$w" ] && break; pgrep -f GTA5_Enhanced >/dev/null || { echo "game exited"; exit; }; sleep 2; done
echo "$w"
# click OK on the driver nag if it shows, then watch for exit
for i in $(seq 1 30); do echo "$w" | grep -q "Minimum" && { vmmap -interleaved $(pgrep -f GTA5_Enhanced | head -1) > ${TMPDIR:-/tmp}/gtavmmap.txt 2>&1; swift $SP/click.swift 713 549; break; }; sleep 2; w=$(swift $SP/winsof.swift $(pgrep -f 'GTA5_Enhanced') 2>/dev/null); done
