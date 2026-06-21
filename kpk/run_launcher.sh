#!/bin/bash
# run_launcher.sh
#
# Shell script to run the launcher
# It is required to run the launcher using this so that the launcher does not exit memory on os.execvp()

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "[launcher] starting"

while true; do
    python3 "$SCRIPT_DIR/launcher.py" # Use a venv for the libraries
    EXIT=$?

    # Exit code 130 = Ctrl-C / KeyboardInterrupt (Reset on the idle screen,
    # which calls kit.interrupt() to actually quit). Give a 2s window to
    # press Reset again to break the loop and quit for real.
    if [ $EXIT -eq 130 ]; then
        echo "[launcher] Reset pressed on idle screen — press again within 2s to quit fully"
        read -t 2 -n 1 && break
    fi

    sleep 0.5
done

echo "[launcher] goodbye"
