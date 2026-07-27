#!/bin/sh
DIR=$(dirname "$0")
. "$DIR/utils.sh"

CPU_LINE=""
case "$(uname -s)" in
    Darwin)
        CPU_LINE=$(top -l 1 -n 0 2>/dev/null | awk '/CPU usage/ { print; exit }')
        ;;
    Linux)
        CPU_LINE=$(top -bn1 2>/dev/null | awk '/^%?Cpu/ { print; exit }')
        ;;
esac

if [ -n "$CPU_LINE" ]; then
    printf '%s\n' "$CPU_LINE"
elif command -v ps >/dev/null 2>&1; then
    PROCESS_CPU=$(ps -A -o %cpu= 2>/dev/null) || PROCESS_CPU=""
    if [ -n "$PROCESS_CPU" ]; then
        printf '%s ' "$(get_msg "script_cpu_process_total" "CPU used by visible processes:")"
        printf '%s\n' "$PROCESS_CPU" | awk '{ total += $1 } END { printf "%.1f%%\n", total }'
    else
        get_msg "script_cpu_missing" "No CPU usage command found (top/ps)."
        exit 1
    fi
else
    get_msg "script_cpu_missing" "No CPU usage command found (top/ps)."
    exit 1
fi
