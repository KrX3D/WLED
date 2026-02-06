#!/bin/bash

# Set low I/O priority to avoid impacting system performance
ionice -c3 -p $(echo $$)

# Configuration for the inter-script lock
LOCKFILE="/var/lock/user-scripts-backup.lock"   # flock lock file (preferred)
LOCKDIR="/var/lock/user-scripts-backup.lockdir" # mkdir fallback lock dir
MAX_WAIT=${MAX_WAIT:-3600}      # max seconds to wait for lock (default 3600s = 1h)
SLEEP_INTERVAL=${SLEEP_INTERVAL:-5}  # seconds between polls

cleanup() {
    # release flock or rmdir, ignore errors
    if [ -n "${USE_FLOCK:-}" ]; then
        # release flock on fd 200, then close fd
        flock -u 200 2>/dev/null || true
        exec 200>&- 2>/dev/null || true
        # remove lock file if present (safe)
        rm -f "$LOCKFILE" 2>/dev/null || true
    else
        [ -n "${LOCK_HELD:-}" ] && rmdir "$LOCKDIR" 2>/dev/null || true
    fi
}
trap cleanup EXIT INT TERM

START_WAIT=$(date +%s)
echo "$(date -Iseconds) Attempting to acquire lock (max wait ${MAX_WAIT}s)..."

# Prefer flock if available
if command -v flock >/dev/null 2>&1; then
    USE_FLOCK=1
    # Open lock fd 200 on LOCKFILE
    exec 200>"$LOCKFILE"
    while ! flock -n 200; do
        now=$(date +%s)
        elapsed=$((now - START_WAIT))
        if [ "$elapsed" -ge "$MAX_WAIT" ]; then
            echo "$(date -Iseconds) Timeout waiting for lock (${elapsed}s). Exiting."
            exit 2
        fi
        echo "$(date -Iseconds) Another instance is running; waiting ${SLEEP_INTERVAL}s..."
        sleep "$SLEEP_INTERVAL"
    done
    echo "$(date -Iseconds) Lock acquired (flock)."
else
    # Fallback: atomic mkdir lock
    while ! mkdir "$LOCKDIR" 2>/dev/null; do
        now=$(date +%s)
        elapsed=$((now - START_WAIT))
        if [ "$elapsed" -ge "$MAX_WAIT" ]; then
            echo "$(date -Iseconds) Timeout waiting for lock (mkdir) (${elapsed}s). Exiting."
            exit 2
        fi
        echo "$(date -Iseconds) Another instance is running (mkdir lock); waiting ${SLEEP_INTERVAL}s..."
        sleep "$SLEEP_INTERVAL"
    done
    LOCK_HELD=1
    echo "$(date -Iseconds) Lock acquired (mkdir)."
fi

# ---------- start timer AFTER acquiring lock ----------
start_time=$(date +%s)

# List of paths to process
paths=(
    "/mnt/user/appdata"
    "/mnt/user/backup"
    "/mnt/user/Daten"
    "/mnt/user/domains"
    "/mnt/user/Filme_Serien"
    "/mnt/user/Filme_Serien2"
    "/mnt/user/isofiles"
    "/mnt/user/PaperlessArchive"
    "/mnt/user/system"
    "/mnt/user/Scripts"
    "/mnt/user/Syslog"
)

echo  # Empty line

# Process each path
for path in "${paths[@]}"; do
    echo "$(date -Iseconds) Processing: $path"

    # Skip if path doesn't exist
    if [ ! -d "$path" ]; then
        echo "$(date -Iseconds) Warning: $path does not exist. Skipping..."
        continue
    fi

    # Check if path has any content before processing
    if [ -z "$(find "$path" -maxdepth 1 -type f -o -type d ! -path "$path" 2>/dev/null | head -1)" ]; then
        echo "$(date -Iseconds) Info: $path appears to be empty. Skipping..."
        continue
    fi

    echo "$(date -Iseconds) Setting directory permissions..."
    # Use xargs with parallel processing for better performance
    find "$path" -type d -print0 | xargs -0 -P 4 chmod u+rwx,g+rwx,o+rwx || true

    echo "$(date -Iseconds) Setting file permissions..."
    # Set base permissions for all files first
    find "$path" -type f -print0 | xargs -0 -P 4 chmod u+rw,g+rw,o+rw || true

    echo "$(date -Iseconds) Adding execute permissions for specific file types..."
    # Add execute permissions for specific file types
    find "$path" -type f \( \
      -name "*.exe" -o -name "*.msi" -o -name "*.msp" -o \
      -name "*.cmd" -o -name "*.bat" -o -name "*.ps1" -o -name "*.psm1" -o \
      -name "*.vbs" -o -name "*.js" -o -name "*.com" -o -name "*.scr" -o \
      -name "*.sh" -o -name "*.py" -o -name "*.pl" -o -name "*.rb" -o \
      -name "*.jar" -o -name "*.bin" -o -name "*.run" -o -name "*.cgi" -o \
      -name "*.fcgi" -o ! -name "*.*" \
    \) -print0 | xargs -0 -r chmod u+x,g+x,o-x || true

    echo "$(date -Iseconds) Permissions updated for $path"

    echo "$(date -Iseconds) Changing ownership..."
    # Use find with xargs for better performance on large file systems
    # Suppress errors for broken symlinks
    find "$path" -print0 | xargs -0 -P 2 chown nobody:users 2>/dev/null || true

    echo "$(date -Iseconds) Ownership changed to nobody:users for $path"

    # Execute specific tasks after certain paths are processed
    if [ "$path" = "/mnt/user/appdata" ]; then
        echo "$(date -Iseconds) Setting ownership for netboot files to 1000:1000"
        if ls /mnt/user/appdata/netboot* >/dev/null 2>&1; then
            find /mnt/user/appdata/netboot* -print0 2>/dev/null | xargs -0 -r chown 1000:1000 || true
            echo "$(date -Iseconds) Netboot files ownership updated"
        else
            echo "$(date -Iseconds) No netboot files found - skipping"
        fi

        echo "$(date -Iseconds) Securing luckybackup SSH key permissions"
        if [ -f /mnt/user/appdata/luckybackup/.ssh/id_rsa ]; then
            chmod 700 /mnt/user/appdata/luckybackup/.ssh 2>/dev/null || true
            chmod 600 /mnt/user/appdata/luckybackup/.ssh/id_rsa 2>/dev/null || true
            echo "$(date -Iseconds) Luckybackup SSH key permissions updated"
        else
            echo "$(date -Iseconds) Luckybackup SSH key not found - skipping"
        fi
    fi

    if [ "$path" = "/mnt/user/backup" ]; then
        TARGET="/mnt/user/backup/Proxmox_Backup_Server/"

        if [ -d "$TARGET" ]; then
            echo "$(date -Iseconds) Changing user/group to 34 for folder: $TARGET"
            find "$TARGET" -print0 | xargs -0 chown 34:34 2>/dev/null || true
            echo "$(date -Iseconds) Proxmox backup folder ownership updated"

            echo "$(date -Iseconds) Setting directory permissions to drwxrwxrwx (0777) ..."
            find "$TARGET" -type d -print0 | xargs -0 chmod 0777 2>/dev/null || true

            echo "$(date -Iseconds) Setting file permissions to rw-rw-rw- (0666) ..."
            find "$TARGET" -type f -print0 | xargs -0 chmod 0666 2>/dev/null || true

            echo "$(date -Iseconds) Permissions updated for $TARGET"
        else
            echo "$(date -Iseconds) Proxmox backup folder not found - skipping"
        fi
    fi

    echo "$(date -Iseconds) Changes synchronized for $path"
    echo  # Empty line for readability
done

# Final sync at the end
echo "$(date -Iseconds) Performing final system sync..."
sync

# Capture the end time
end_time=$(date +%s)

# Calculate the elapsed time
elapsed_time=$((end_time - start_time))

# Convert elapsed time to hours, minutes, and seconds
if [ $elapsed_time -ge 3600 ]; then
    hours=$((elapsed_time / 3600))
    minutes=$(( (elapsed_time % 3600) / 60 ))
    seconds=$((elapsed_time % 60))
    echo "$(date -Iseconds) Completed, elapsed time: $hours hours, $minutes minutes, $seconds seconds"
elif [ $elapsed_time -ge 60 ]; then
    minutes=$((elapsed_time / 60))
    seconds=$((elapsed_time % 60))
    echo "$(date -Iseconds) Completed, elapsed time: $minutes minutes, $seconds seconds"
else
    echo "$(date -Iseconds) Completed, elapsed time: $elapsed_time seconds"
fi

# cleanup trap will run here to release lock
exit 0
