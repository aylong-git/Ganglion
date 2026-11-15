#!/bin/bash
cd "$(dirname "$0")"
./build.sh
status=$?
if [ "$status" -ne 0 ]; then
    echo
    echo "Build failed. Press Return to close this window."
    read -r _
fi
exit "$status"
