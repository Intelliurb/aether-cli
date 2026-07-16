# Shared variables
AETHERCLI_CONFIG="${AETHERCLI_CONFIG:-/usr/local/etc/aethercli/config.json}"
AETHERCLI_USERS_FILE="${AETHERCLI_USERS_FILE:-/usr/local/etc/aethercli/users.json}"

LANG_FILE="$(dirname "$AETHERCLI_CONFIG")/lang_${AETHERCLI_LANG:-en}.json"

get_msg() {
    local key="$1"
    local default_msg="$2"
    if [ -f "$LANG_FILE" ]; then
        local msg
        msg=$(jq -r ".\"$key\" // \"$default_msg\"" "$LANG_FILE" 2>/dev/null)
        if [ "$msg" != "null" ] && [ -n "$msg" ]; then
            echo "$msg"
            return
        fi
    fi
    echo "$default_msg"
}

check_write_permission() {
    local file="$1"
    if ! touch "$file" 2>/dev/null; then
        local msg
        msg=$(get_msg "script_no_write_perm" "%% Error: Permission denied. Cannot write to %s. Try running aethercli with sudo.")
        # Replace %s with the filename manually if printf is not guaranteed
        echo "${msg%%%s*}$file${msg#*%s}" >&2
        exit 1
    fi
}
