# Retire la layer du registre OpenXR et supprime la tâche de démarrage de XRPerfSensors.
# Les fichiers ne sont pas effacés : supprimez ensuite le dossier à la main.
$ErrorActionPreference = 'Stop'

# Langue des messages : français pour un Windows en français, anglais sinon.
$script:English = ([System.Globalization.CultureInfo]::CurrentUICulture.TwoLetterISOLanguageName -ne 'fr')
function T([string]$French, [string]$English) { if ($script:English) { return $English } return $French }
$keyPath = 'SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit'

$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$isAdmin = (New-Object Security.Principal.WindowsPrincipal $identity).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Start-Process powershell -Verb RunAs -ArgumentList @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$($MyInvocation.MyCommand.Path)`"")
    return
}

try {
    $removed = 0
    $hklm = [Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::LocalMachine, [Microsoft.Win32.RegistryView]::Registry64)
    $key = $hklm.OpenSubKey($keyPath, $true)
    if ($key) {
        foreach ($name in $key.GetValueNames()) {
            if ($name.EndsWith('XR_APILAYER_NOVENDOR_perf_overlay.json', [StringComparison]::OrdinalIgnoreCase)) {
                $key.DeleteValue($name)
                Write-Host ((T "Inscription retirée : " "Registration removed: ") + $name) -ForegroundColor Green
                $removed++
            }
        }
        $key.Close()
    }
    $hklm.Close()
    if ($removed -eq 0) { Write-Host (T "La layer n'était pas enregistrée." "The layer was not registered.") }

    if (Get-ScheduledTask -TaskName 'XRPerfSensors' -ErrorAction SilentlyContinue) {
        Stop-ScheduledTask -TaskName 'XRPerfSensors' -ErrorAction SilentlyContinue
        Unregister-ScheduledTask -TaskName 'XRPerfSensors' -Confirm:$false
        Write-Host (T "Tâche planifiée 'XRPerfSensors' supprimée." "Scheduled task 'XRPerfSensors' removed.") -ForegroundColor Green
    }
    Get-Process -Name 'XRPerfSensors' -ErrorAction SilentlyContinue | Stop-Process -Force
    # Source OBS (plugin copié par Installer.bat) : retirée d'OBS Studio.
    $obsPlugin = Join-Path (Join-Path (Join-Path $env:ProgramData 'obs-studio') 'plugins') 'xrperf-obs'
    if (Test-Path -LiteralPath $obsPlugin) {
        try {
            Remove-Item -LiteralPath $obsPlugin -Recurse -Force
            Write-Host (T "Source OBS retirée d'OBS Studio." "OBS source removed from OBS Studio.") -ForegroundColor Green
        } catch {
            Write-Host (T "Source OBS non retirée (OBS Studio est ouvert ?) : supprimez $obsPlugin à la main." "OBS source not removed (is OBS Studio open?): delete $obsPlugin by hand.") -ForegroundColor Yellow
        }
    }
    # XRPerfVR s'arrête de lui-même avec XRPerfSensors, après avoir terminé son rapport.
    foreach ($process in @(Get-Process -Name 'XRPerfVR' -ErrorAction SilentlyContinue)) {
        try { if (-not $process.WaitForExit(5000)) { $process.Kill() } } catch { }
    }
} catch {
    Write-Host ((T "ERREUR : " "ERROR: ") + $_.Exception.Message) -ForegroundColor Red
}
Read-Host 'Appuyez sur Entrée pour fermer'
