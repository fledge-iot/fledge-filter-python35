#!/usr/bin/env bash
set -euo pipefail

export FLEDGE_ROOT=/usr/local/fledge
export FLEDGE_DATA="${FLEDGE_DATA:-/usr/local/fledge/data}"

# Fledge logs to syslog
rm -f /var/run/rsyslogd.pid
touch /var/log/syslog
# Ubuntu's rsyslog drops privileges to the syslog user, Debian's stays root
chown syslog:adm /var/log/syslog 2>/dev/null || chmod 666 /var/log/syslog
rsyslogd

"${FLEDGE_ROOT}/bin/fledge" start

# Keep the container in the foreground and show the Fledge logs
exec tail -F /var/log/syslog
