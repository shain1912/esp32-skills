# Run elevated (see SKILL.md step 2). Idempotent: safe to re-run.
# Appends the LAN + WebSocket listener block to the Mosquitto Windows
# service's config and restarts the service so it takes effect.
$ErrorActionPreference = 'Stop'
$logPath = Join-Path $PSScriptRoot 'setup_result.txt'
$confPath = 'C:\Program Files\Mosquitto\mosquitto.conf'

try {
    $existing = Get-Content -Path $confPath -Raw -ErrorAction SilentlyContinue
    if ($existing -notmatch 'listener 9001') {
        Add-Content -Path $confPath -Encoding utf8 -Value @'

# --- XIAO ESP32S3 dashboard: LAN + WebSocket listeners ---
allow_anonymous true
listener 1883 0.0.0.0
listener 9001 0.0.0.0
protocol websockets
'@
        'appended config' | Out-File -FilePath $logPath -Encoding utf8
    } else {
        'config already present' | Out-File -FilePath $logPath -Encoding utf8
    }

    Restart-Service -Name mosquitto -Force
    Start-Sleep -Seconds 2
    'service restarted OK' | Out-File -FilePath $logPath -Append -Encoding utf8
} catch {
    "ERROR: $($_.Exception.Message)" | Out-File -FilePath $logPath -Append -Encoding utf8
}
