#!/bin/zsh
# Rebuild OpenV's native map after a GTA V Enhanced update. Double-click this file in Finder.
# It opens GTA V, copies what it needs from the running game, closes it, and builds the new map.
# A full log goes to rebuild-map.log next to this file.

OPENV=${0:A:h}
LOG="$OPENV/rebuild-map.log"
VODKA="$HOME/Library/Containers/com.rolling7ho.Vodka/Bottles"
WINEBIN="$HOME/Library/Application Support/com.rolling7ho.Vodka/Libraries/Wine/bin"
GAME_EXE="GTA5_Enhanced.exe"
STEAM_APP=3240220
WORK=$(mktemp -d /tmp/openv-rebuild.XXXXXX)
STARTED_GAME=0
: > "$LOG"

say()  { print -- "$*" | tee -a "$LOG"; }
line() { say "------------------------------------------------------------"; }
notify() { osascript -e "display notification \"$1\" with title \"OpenV\"" >/dev/null 2>&1; }
game_pid() { pgrep -f "${GAME_EXE//./\\.}" | head -1; }
finish() {   # finish <exit code> <headline> <explanation...>
    local code=$1 head=$2; shift 2
    if (( STARTED_GAME )) && [[ -n "$(game_pid)" ]]; then kill "$(game_pid)" 2>/dev/null; fi
    print; line
    if (( code == 0 )); then say "  ✅  $head"; else say "  ❌  $head"; fi
    line
    for l in "$@"; do say "  $l"; done
    print
    say "  (Details for troubleshooting are in: $LOG)"
    notify "$head"
    rm -rf "$WORK"
    [[ -t 0 ]] && { print; read -k1 "?Press any key to close this window."; }
    exit $code
}
trap 'finish 1 "Stopped." "The rebuild was cancelled. Nothing was changed."' INT

clear
say "============================================================"
say "  OpenV map rebuilder"
say "============================================================"
say ""
say "  Use this after GTA V updates, when Menyoo or other mods stop"
say "  working. It teaches OpenV the new game version."
say ""
say "  It takes about 20 minutes. GTA V will open by itself and"
say "  close by itself. You don't need to click anything in the game."
say ""
say "  If you are playing GTA V right now, save and quit first."
say ""
[[ -t 0 ]] && read -k1 "?  Press any key to start (or close this window to cancel). "
print; print

# ---------------------------------------------------------------- 1. setup
say "Step 1 of 4: Checking your setup"
GAME_DIR=$(print -l "$VODKA"/*/drive_c/"Program Files (x86)"/Steam/steamapps/common/"Grand Theft Auto V Enhanced"(N/) | head -1)
[[ -n "$GAME_DIR" ]] || finish 1 "GTA V Enhanced was not found." \
    "This looks for GTA V Enhanced inside a Vodka bottle with Steam." \
    "Make sure the game is installed in Vodka, then try again."
BOTTLE=${GAME_DIR%%/drive_c/*}
[[ -x "$WINEBIN/wine" ]] || finish 1 "Vodka's Wine was not found." "Open Vodka once so it can finish setting up, then try again."

if [[ ! -x "$OPENV/.venv/bin/python" ]] || ! "$OPENV/.venv/bin/python" -I -c "import capstone" 2>>"$LOG"; then
    say "  Installing the analysis tools (one time only, needs internet)..."
    { python3 -m venv "$OPENV/.venv" && "$OPENV/.venv/bin/pip" install -q capstone; } >>"$LOG" 2>&1 \
        || finish 1 "Could not install the analysis tools." \
               "This step needs an internet connection and Apple's command line tools." \
               "If a window asked to install 'command line developer tools', accept it, wait" \
               "for it to finish, then run this again."
fi
PY="$OPENV/.venv/bin/python"

VERSION=$("$PY" -I -c "
import sys, struct
d = open(sys.argv[1], 'rb').read(); i = d.find(struct.pack('<I', 0xFEEF04BD))
ms, ls = struct.unpack_from('<II', d, i + 8); print(f'{ls >> 16}.{ls & 0xffff}')" "$GAME_DIR/$GAME_EXE" 2>>"$LOG")
[[ -n "$VERSION" ]] || finish 1 "Could not read the game's version." "Try verifying the game files in Steam, then run this again."
MAP="$GAME_DIR/OpenV/natives-$VERSION.txt"
say "  Game version: $VERSION"
if [[ -s "$MAP" && "$1" != "--force" ]]; then
    finish 0 "Already up to date." \
        "OpenV already has a map for game version $VERSION, so there is nothing to do." \
        "If mods still don't work, the problem is something else (for example the mod" \
        "itself needs an update)."
fi
say "  No map yet for version $VERSION. Let's make one."
print

# ---------------------------------------------------------------- 2. open the game
say "Step 2 of 4: Opening GTA V"
if [[ -z "$(game_pid)" ]]; then
    say "  Starting the game through Vodka. If Vodka asks whether to launch it,"
    say "  click Launch. If GTA shows a message about graphics drivers, click OK."
    open "vodka://launch?steam=$STEAM_APP"; STARTED_GAME=1
    for i in {1..150}; do [[ -n "$(game_pid)" ]] && break; sleep 2; done
fi
PID=$(game_pid)
[[ -n "$PID" ]] || finish 1 "GTA V did not start." \
    "Try starting GTA V from Vodka yourself. Once you see the main menu, run this again" \
    "(it will use the game that is already open)."
say "  The game is running. Waiting for it to finish loading (a few minutes)..."

cp "$OPENV/tools/dump/openv-dump.exe" "$OPENV/tools/dump/openv-natives.exe" "$WORK/"
cd "$WORK"
# The game's Wine may run in msync or esync mode, and the helper tools must match it. macOS hides other
# processes' environment, so read Wine's own complaint about a mismatch and retry in the mode it names.
wine() {
    local rc
    for try in 1 2 3; do
        "$WINEBIN/wine" "$@" 2>"$WORK/wine.err"; rc=$?
        cat "$WORK/wine.err" >>"$LOG"
        if   grep -q "Server is running with WINEMSYNC" "$WORK/wine.err"; then export WINEMSYNC=1; unset WINEESYNC
        elif grep -q "Server is running with WINEESYNC" "$WORK/wine.err"; then export WINEESYNC=1; unset WINEMSYNC
        elif grep -q "Server is not running with WINE.SYNC" "$WORK/wine.err"; then unset WINEMSYNC WINEESYNC
        else return $rc; fi
    done
    return $rc
}
# GTA sometimes opens its window half off-screen, and then stops loading until it is visible.
show_game() {
    osascript -e "tell application \"System Events\" to tell (first process whose unix id is $1)
        set frontmost to true
        if position of window 1 is not {0, 0} then set position of window 1 to {0, 0}
    end tell" >>"$LOG" 2>&1
}

# Loaded once the game's native table is complete: OpenV's log says so, or, without OpenV, give it time.
# Steam restarts the game process once during launch, so only a long absence counts as a crash.
START=$(date +%s); GONE=0
while true; do
    PID=$(game_pid)
    if [[ -z "$PID" ]]; then
        (( ++GONE > 8 )) && finish 1 "GTA V closed or crashed while loading." \
            "That sometimes happens on its own. Just run this again."
    else
        GONE=0; show_game "$PID"
        (( STARTED_GAME )) || break       # it was already open before we started: no need to wait
    fi
    if [[ -f "$GAME_DIR/openv.log" && $(stat -f %m "$GAME_DIR/openv.log") -ge $START ]] \
       && grep -q "INIT: natives:\|INIT: FAILED" "$GAME_DIR/openv.log"; then break; fi
    (( $(date +%s) - START > 600 )) && break
    sleep 5
done
sleep 20
print

# ---------------------------------------------------------------- 3. copy what we need from the game
say "Step 3 of 4: Copying game data"
export WINEPREFIX="$BOTTLE" WINEDEBUG=-all,err+sync
FOUND=""
for attempt in 1 2 3 4 5; do
    [[ -n "$(game_pid)" ]] || break
    rm -f image.bin
    wine openv-dump.exe "$GAME_EXE" image.bin >>"$LOG" || true
    if [[ -s image.bin ]]; then
        for rva in $("$PY" -I "$OPENV/research/findtable.py" "$WORK/image.bin" 2>>"$LOG"); do
            out=$(wine openv-natives.exe "$rva" natives.tsv)
            print -- "attempt $attempt, table candidate $rva: $out" >>"$LOG"
            n=$(print -- "$out" | sed -n 's/^natives \([0-9]*\).*/\1/p')
            if (( ${n:-0} > 6000 )); then FOUND=$rva; break; fi
        done
    fi
    [[ -n "$FOUND" ]] && break
    say "  The game isn't ready yet, trying again in 30 seconds..."
    sleep 30
done
[[ -s image.bin ]] || finish 1 "Could not read the running game." \
    "Close GTA V, quit and reopen Vodka, then run this again."
OPENV_BROKEN=0
grep -q "game internals not found" "$GAME_DIR/openv.log" 2>/dev/null && OPENV_BROKEN=1

say "  Closing GTA V..."
kill "$(game_pid)" 2>/dev/null; sleep 5
[[ -n "$(game_pid)" ]] && kill -9 "$(game_pid)" 2>/dev/null

[[ -n "$FOUND" ]] || finish 1 "This update changed the game too much for the automatic fix." \
    "OpenV could not find the game's list of functions. Mods will stay off until" \
    "OpenV itself is updated. The game still works normally without mods."
print

# ---------------------------------------------------------------- 4. build the map
say "Step 4 of 4: Building the new map (about 15 minutes, the window may look idle)"
cd "$OPENV"
"$PY" -I research/rebuild.py "$WORK/image.bin" "$WORK/natives.tsv" "$VERSION" "$WORK/natives-$VERSION.txt" "$WORK" 2>>"$LOG" | tee -a "$LOG"
result=${pipestatus[1]}
if (( result == 2 )); then
    finish 1 "The new map wasn't reliable enough to use." \
        "This update changed too much for the automatic fix. To stay safe, OpenV keeps" \
        "mods off for this version. The game still works normally without mods."
elif (( result != 0 )); then
    finish 1 "Something went wrong while building the map." "Nothing was changed. You can try running this again."
fi

mkdir -p "$GAME_DIR/OpenV" "$OPENV/data"
cp "$WORK/natives-$VERSION.txt" "$MAP" && cp "$WORK/natives-$VERSION.txt" "$OPENV/data/"
if (( OPENV_BROKEN )); then
    finish 1 "Map built, but OpenV itself also needs an update." \
        "The map for version $VERSION is saved, but this update also changed something" \
        "else OpenV relies on, so mods will stay off until OpenV is updated." \
        "The game still works normally without mods."
fi
finish 0 "Done! Mods should work again." \
    "OpenV now knows game version $VERSION." \
    "Start GTA V from Vodka like normal. In story mode, press F8 (twice the first time)" \
    "to open Menyoo."
