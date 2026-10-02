#!/bin/bash
# Routes the LOCAL0 syslog facility (used by disk_monitor) into a separate log file.
set -euo pipefail

if [ "$EUID" -ne 0 ]; then
    echo "Please run the script with root privileges (sudo ./setup_logs.sh)"
    exit 1
fi

if ! command -v rsyslogd > /dev/null; then
    echo "rsyslog is not installed. Install it (e.g. 'sudo apt install rsyslog') or read the"
    echo "log from journald: journalctl -t disk-monitor -f"
    exit 1
fi

LOG_FILE="/var/log/disk-monitor.log"
CONF_FILE="/etc/rsyslog.d/20-disk-monitor.conf"

echo "Setting up rsyslog for LOG_LOCAL0..."

# '& stop' prevents the same messages from being duplicated into /var/log/syslog.
cat > "$CONF_FILE" << 'RSYSLOG_CONF'
local0.*    /var/log/disk-monitor.log
& stop
RSYSLOG_CONF

touch "$LOG_FILE"
chmod 644 "$LOG_FILE"

# rsyslog drops privileges on Debian/Ubuntu, so the file must be writable by the syslog user.
if id syslog > /dev/null 2>&1; then
    chown syslog:adm "$LOG_FILE"
fi

systemctl restart rsyslog

echo "Done! Now the daemon logs will be written to $LOG_FILE"
