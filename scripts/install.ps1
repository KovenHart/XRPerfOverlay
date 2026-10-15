# Enregistre la layer auprès d'OpenXR (clé de registre standard des "API layers" implicites).
# Option -AutoStartSensors : lance XRPerfSensors à chaque ouverture de session Windows, sans invite UAC.
# Les options -Quiet et -User servent à Installer.bat (scripts\setup.ps1), qui appelle ce script.
param(
    [switch]$AutoStartSensors,
    # Pas de pause à la fin, et l'erreur éventuelle est transmise à l'appelant.
    [switch]$Quiet,
    # Compte Windows pour lequel XRPerfSensors doit démarrer (par défaut : le compte courant).
    [string]$User = ''
)

$ErrorActionPreference = 'Stop'
$dir = Split-Path -Parent $MyInvocation.MyCommand.Path
$manifest = Join-Path $dir 'XR_APILAYER_NOVENDOR_perf_overlay.json'
$dll = Join-Path $dir 'XR_APILAYER_NOVENDOR_perf_overlay.dll'
$sensors = Join-Path $dir 'sensors\XRPerfSensors.exe'
$keyPath = 'SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit'

# L'écriture dans HKLM demande les droits administrateur : on se relance en élevé si besoin.
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$isAdmin = (New-Object Security.Principal.WindowsPrincipal $identity).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    if ($Quiet) { throw "Droits administrateur nécessaires pour enregistrer la layer." }
    $arguments = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$($MyInvocation.MyCommand.Path)`"")
    if ($AutoStartSensors) { $arguments += '-AutoStartSensors' }
    Start-Process powershell -Verb RunAs -ArgumentList $arguments
    return
}

try {
    if (-not (Test-Path -LiteralPath $manifest) -or -not (Test-Path -LiteralPath $dll)) {
        throw "Fichiers de la layer introuvables dans $dir. Lancez ce script depuis le dossier dist\ produit par build.bat."
    }

    # Vue 64 bits du registre, quel que soit le PowerShell qui exécute ce script : c'est celle que lisent les jeux.
    $hklm = [Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::LocalMachine, [Microsoft.Win32.RegistryView]::Registry64)
    $key = $hklm.CreateSubKey($keyPath)
    try {
        # Retire d'éventuelles anciennes inscriptions de cette layer faites depuis un autre dossier.
        foreach ($name in $key.GetValueNames()) {
            if ($name.EndsWith('XR_APILAYER_NOVENDOR_perf_overlay.json', [StringComparison]::OrdinalIgnoreCase) -and $name -ne $manifest) {
                $key.DeleteValue($name)
                Write-Host "Ancienne inscription retirée : $name"
            }
        }
        # Nom de la valeur = chemin complet du fichier .json ; DWORD 0 = layer activée (1 = désactivée).
        $key.SetValue($manifest, 0, [Microsoft.Win32.RegistryValueKind]::DWord)
    } finally {
        $key.Close()
        $hklm.Close()
    }
    Write-Host "Layer enregistrée : $manifest" -ForegroundColor Green

    if ($AutoStartSensors) {
        if (-not (Test-Path -LiteralPath $sensors)) { throw "XRPerfSensors.exe introuvable ($sensors)." }
        if (-not $User) { $User = "$env:USERDOMAIN\$env:USERNAME" }
        $action = New-ScheduledTaskAction -Execute $sensors
        $trigger = New-ScheduledTaskTrigger -AtLogOn -User $User
        $principal = New-ScheduledTaskPrincipal -UserId $User -LogonType Interactive -RunLevel Highest
        $settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -ExecutionTimeLimit ([TimeSpan]::Zero)
        Register-ScheduledTask -TaskName 'XRPerfSensors' -Action $action -Trigger $trigger -Principal $principal -Settings $settings -Force | Out-Null
        Start-ScheduledTask -TaskName 'XRPerfSensors'
        Write-Host "XRPerfSensors démarrera à chaque ouverture de session (tâche planifiée 'XRPerfSensors')." -ForegroundColor Green
    } elseif (-not $Quiet) {
        Write-Host "Pensez à lancer sensors\XRPerfSensors.exe avant de jouer (ou relancez : install.bat -AutoStartSensors)."
    }
} catch {
    if ($Quiet) { throw }
    Write-Host "ERREUR : $($_.Exception.Message)" -ForegroundColor Red
}
if (-not $Quiet) { Read-Host 'Appuyez sur Entrée pour fermer' }
