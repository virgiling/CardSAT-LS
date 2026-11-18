#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: run_experiments.sh SOLVER BENCHMARK_DIR RESULT_DIR [SOLVER_OPTION ...]

The benchmark directory must contain satvbs.txt. Relative entries in that
file are resolved against BENCHMARK_DIR. Each solver run is written to a
separate .log file in RESULT_DIR.

Environment variables:
  JOBS=1                  Maximum concurrent solver processes.
  TIMEOUT_SECONDS=3600    Per-instance wall-clock limit.
  MEMORY_LIMIT_KIB=0      Per-process virtual-memory limit; 0 disables it.
  TIMEOUT_COMMAND=timeout GNU timeout command (use gtimeout on macOS).
  INSTANCE_LIST=...       Override BENCHMARK_DIR/satvbs.txt.
EOF
}

if [[ ${1:-} == "-h" || ${1:-} == "--help" ]]; then
    usage
    exit 0
fi

if (( $# < 3 )); then
    usage >&2
    exit 2
fi

solver=$1
benchmark_dir=$2
result_dir=$3
shift 3
solver_args=("$@")

jobs=${JOBS:-1}
timeout_seconds=${TIMEOUT_SECONDS:-3600}
memory_limit_kib=${MEMORY_LIMIT_KIB:-0}
timeout_command=${TIMEOUT_COMMAND:-timeout}
instance_list=${INSTANCE_LIST:-"$benchmark_dir/satvbs.txt"}

[[ -x "$solver" ]] || { echo "solver is not executable: $solver" >&2; exit 2; }
[[ -f "$instance_list" ]] || { echo "instance list not found: $instance_list" >&2; exit 2; }
[[ "$jobs" =~ ^[1-9][0-9]*$ ]] || { echo "JOBS must be a positive integer" >&2; exit 2; }
[[ "$timeout_seconds" =~ ^[1-9][0-9]*$ ]] || { echo "TIMEOUT_SECONDS must be a positive integer" >&2; exit 2; }
[[ "$memory_limit_kib" =~ ^[0-9]+$ ]] || { echo "MEMORY_LIMIT_KIB must be a non-negative integer" >&2; exit 2; }
command -v "$timeout_command" >/dev/null 2>&1 || {
    echo "timeout command not found: $timeout_command" >&2
    exit 2
}
command -v sha256sum >/dev/null 2>&1 || {
    echo "sha256sum is required to create collision-resistant log names" >&2
    exit 2
}
[[ -x /usr/bin/time ]] || { echo "/usr/bin/time is required" >&2; exit 2; }

mkdir -p "$result_dir"
semaphore_dir=$(mktemp -d "${TMPDIR:-/tmp}/cardsat-run.XXXXXX")
semaphore_fifo="$semaphore_dir/tokens"
mkfifo "$semaphore_fifo"
exec 9<>"$semaphore_fifo"
rm "$semaphore_fifo"
rmdir "$semaphore_dir"

for ((i = 0; i < jobs; ++i)); do
    printf '.\n' >&9
done

terminate_children() {
    trap - INT TERM
    for pid in $(jobs -pr); do
        kill "$pid" 2>/dev/null || true
    done
    wait || true
    exec 9>&-
    exit "$1"
}
trap 'terminate_children 130' INT
trap 'terminate_children 143' TERM

while IFS= read -r instance || [[ -n "$instance" ]]; do
    [[ -z "$instance" || "$instance" == \#* ]] && continue
    read -r -u 9 _token
    {
        input=$instance
        [[ "$input" = /* ]] || input="$benchmark_dir/$input"
        instance_hash=$(printf '%s' "$instance" | sha256sum)
        instance_hash=${instance_hash%% *}
        log_path="$result_dir/${instance_hash}.log"

        if [[ ! -f "$input" ]]; then
            printf 'runner_instance %s\ninstance not found\nrunner_exit_status 66\n' \
                "$input" >"$log_path"
        else
            set +e
            printf 'runner_instance %s\n' "$input" >"$log_path"
            (
                if (( memory_limit_kib > 0 )); then
                    if ! ulimit -v "$memory_limit_kib"; then
                        echo "failed to apply MEMORY_LIMIT_KIB" >&2
                        exit 125
                    fi
                fi
                /usr/bin/time -p "$timeout_command" "$timeout_seconds" \
                    "$solver" "$input" "${solver_args[@]}"
            ) >>"$log_path" 2>&1
            status=$?
            printf 'runner_exit_status %d\n' "$status" >>"$log_path"
            set -e
        fi
        printf '.\n' >&9
    } &
done <"$instance_list"

wait
exec 9>&-
