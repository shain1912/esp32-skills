# Run elevated (see SKILL.md step 2). Idempotent: safe to re-run.
# Opens inbound TCP 1883 (MQTT) and 9001 (WebSocket) so LAN devices —
# especially boards, which are on a "Public" Windows network profile by
# default — can actually reach the broker. Opening the listener in
# mosquitto.conf is NOT enough; Windows Firewall blocks unmatched inbound
# connections regardless.
$ErrorActionPreference = 'Stop'
$logPath = Join-Path $PSScriptRoot 'setup_result.txt'

try {
    if (-not (Get-NetFirewallRule -DisplayName 'ESP32 MQTT (1883)' -ErrorAction SilentlyContinue)) {
        New-NetFirewallRule -DisplayName 'ESP32 MQTT (1883)' -Direction Inbound -Protocol TCP -LocalPort 1883 -Action Allow -Profile Any | Out-Null
    }
    if (-not (Get-NetFirewallRule -DisplayName 'ESP32 MQTT WebSocket (9001)' -ErrorAction SilentlyContinue)) {
        New-NetFirewallRule -DisplayName 'ESP32 MQTT WebSocket (9001)' -Direction Inbound -Protocol TCP -LocalPort 9001 -Action Allow -Profile Any | Out-Null
    }
    'firewall rules created OK' | Out-File -FilePath $logPath -Encoding utf8
} catch {
    "ERROR: $($_.Exception.Message)" | Out-File -FilePath $logPath -Encoding utf8
}
