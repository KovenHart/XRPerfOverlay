# Installe ou met à jour XR Perf Overlay à partir du dossier produit par la compilation.
# Lancé par Installer.bat ; compatible avec Windows PowerShell 5.1.
#
# Ce que fait ce script, dans l'ordre :
#   1. vérifie que le dossier compilé est complet et qu'aucun jeu n'utilise encore l'ancienne DLL ;
#   2. arrête XRPerfSensors ;
#   3. copie les fichiers dans le dossier d'installation, en gardant vos réglages (XRPerfOverlay.ini)
#      et vos résumés de session (sessions\) ; le sous-dossier sensors\ est remplacé en entier ;
#   4. enregistre la layer auprès d'OpenXR et relance XRPerfSensors.
# Rien n'est modifié avant la fin de l'étape 1.
param(
    # Dossier produit par build.bat (DLL, .json, .ini, scripts, sensors\).
    [string]$Source = '',
    # Dossier d'installation. Vide : celui de l'installation existante, sinon C:\XRPerfOverlay.
    [string]$Target = '',
    # Compte Windows de l'utilisateur (renseigné automatiquement avant la demande d'élévation).
    [string]$User = '',
    # Démarrage automatique de XRPerfSensors : 'oui', 'non', ou vide pour poser la question.
    [string]$AutoStart = '',
    # Interne : le script a déjà été relancé avec les droits administrateur.
    [switch]$Elevated
)

$ErrorActionPreference = 'Stop'
$script:Relaunched = $false

$LayerDll = 'XR_APILAYER_NOVENDOR_perf_overlay.dll'
$LayerJson = 'XR_APILAYER_NOVENDOR_perf_overlay.json'
$IniName = 'XRPerfOverlay.ini'
$IniTemplateName = 'XRPerfOverlay.defaut.ini'
$TaskName = 'XRPerfSensors'
$ScriptFiles = @('install.ps1', 'install.bat', 'uninstall.ps1', 'uninstall.bat')

# --- Fonctions propres à Windows (registre, tâches planifiées, processus) -------------------------

function Test-IsAdmin {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    return (New-Object Security.Principal.WindowsPrincipal $identity).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

# Dossier de l'installation déjà enregistrée auprès d'OpenXR, ou chaîne vide.
function Get-RegisteredFolder {
    $found = ''
    $hklm = [Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::LocalMachine, [Microsoft.Win32.RegistryView]::Registry64)
    try {
        $key = $hklm.OpenSubKey('SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit')
        if ($key) {
            foreach ($name in $key.GetValueNames()) {
                if ($name.EndsWith($LayerJson, [StringComparison]::OrdinalIgnoreCase)) {
                    $folder = Split-Path -Parent $name
                    if ($folder -and (Test-Path -LiteralPath $folder -PathType Container)) { $found = $folder }
                }
            }
            $key.Close()
        }
    } finally {
        $hklm.Close()
    }
    return $found
}

function Test-AutoStartTask {
    return [bool](Get-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue)
}

# Arrête XRPerfSensors. Retourne $true s'il tournait.
function Stop-Sensors {
    $running = @(Get-Process -Name 'XRPerfSensors' -ErrorAction SilentlyContinue)
    if (Test-AutoStartTask) { Stop-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue }
    if ($running.Count -gt 0) {
        $running | Stop-Process -Force -ErrorAction SilentlyContinue
        # Les fichiers ne sont libérés qu'une fois le processus réellement terminé.
        foreach ($process in $running) {
            try { [void]$process.WaitForExit(5000) } catch { }
        }
    }
    # XRPerfVR (jeux SteamVR), rangé dans le même dossier, s'arrête de lui-même avec XRPerfSensors
    # après avoir terminé son rapport : on lui en laisse le temps, puis il est arrêté de force.
    foreach ($process in @(Get-Process -Name 'XRPerfVR' -ErrorAction SilentlyContinue)) {
        try {
            if (-not $process.WaitForExit(5000)) { $process.Kill(); [void]$process.WaitForExit(2000) }
        } catch { }
    }
    return ($running.Count -gt 0)
}

# Relance la version déjà installée de XRPerfSensors (installation abandonnée avant toute copie).
function Restart-Sensors([string]$Folder) {
    try {
        if (Test-AutoStartTask) { Start-ScheduledTask -TaskName $TaskName } else { Start-Sensors $Folder }
    } catch { }
}

# Enregistre la layer (et, si demandé, le démarrage automatique) avec le script du dossier installé.
function Register-Layer([string]$Folder, [bool]$WithAutoStart, [string]$Account) {
    $script = Join-Path $Folder 'install.ps1'
    if ($WithAutoStart) {
        & $script -Quiet -User $Account -AutoStartSensors
    } else {
        # Démarrage automatique refusé : une tâche créée par une installation précédente est retirée.
        if (Test-AutoStartTask) { Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false }
        & $script -Quiet -User $Account
    }
}

# Fichiers venus d'une archive téléchargée : Windows les marque « provenant d'Internet » et peut
# demander une confirmation à chaque lancement de XRPerfSensors. L'utilisateur vient d'accepter
# l'installation : la marque est retirée des fichiers installés (pas des réglages ni des rapports).
function Unblock-Installed([string]$Folder, [string[]]$Names) {
    if (-not (Get-Command Unblock-File -ErrorAction SilentlyContinue)) { return }
    try {
        foreach ($name in $Names) {
            $path = Join-Path $Folder $name
            if (Test-Path -LiteralPath $path -PathType Leaf) { Unblock-File -LiteralPath $path -ErrorAction SilentlyContinue }
        }
        $sensors = Join-Path $Folder 'sensors'
        if (Test-Path -LiteralPath $sensors -PathType Container) {
            Get-ChildItem -LiteralPath $sensors -Recurse -File -ErrorAction SilentlyContinue | Unblock-File -ErrorAction SilentlyContinue
        }
    } catch { }
}

function Start-Sensors([string]$Folder) {
    Start-Process -FilePath (Join-Path (Join-Path $Folder 'sensors') 'XRPerfSensors.exe')
}

# Vérifie, après un court délai, que le programme de capteurs tourne bien.
function Test-SensorsRunning {
    for ($i = 0; $i -lt 10; $i++) {
        Start-Sleep -Milliseconds 500
        if (@(Get-Process -Name 'XRPerfSensors' -ErrorAction SilentlyContinue).Count -gt 0) { return $true }
    }
    return $false
}

# --- Fichiers ------------------------------------------------------------------------------------

# $true tant que le fichier ne peut pas être remplacé : c'est le cas d'une DLL chargée par un jeu ou
# d'un programme en cours d'exécution. Les simples lecteurs (antivirus, indexation) ne comptent pas.
function Test-FileLocked([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return $false }
    try {
        $share = [System.IO.FileShare]::ReadWrite -bor [System.IO.FileShare]::Delete
        $stream = [System.IO.File]::Open($Path, [System.IO.FileMode]::Open, [System.IO.FileAccess]::ReadWrite, $share)
        $stream.Close()
        return $false
    } catch {
        return $true
    }
}

# Laisse quelques secondes à un programme qu'on vient d'arrêter pour libérer son fichier.
function Wait-FileUnlocked([string]$Path, [int]$Tries = 25) {
    for ($i = 0; $i -lt $Tries; $i++) {
        if (-not (Test-FileLocked $Path)) { return $true }
        Start-Sleep -Milliseconds 200
    }
    return $false
}

# Chemin complet sans séparateur final, pour comparer deux dossiers.
function Get-FullPath([string]$Path) {
    $full = [System.IO.Path]::GetFullPath($Path)
    $trimmed = $full.TrimEnd([System.IO.Path]::DirectorySeparatorChar, [System.IO.Path]::AltDirectorySeparatorChar)
    if ($trimmed.Length -lt 3) { return $full }  # racine d'un disque : on garde "C:\"
    return $trimmed
}

function Test-SamePath([string]$A, [string]$B) {
    return [string]::Equals($A, $B, [StringComparison]::OrdinalIgnoreCase)
}

# $true si $Folder contient les sources (c'est le dossier de Installer.bat).
function Test-HasSources([string]$Folder) {
    if (-not $Folder) { return $false }
    if (Test-Path -LiteralPath (Join-Path (Join-Path $Folder 'layer') 'CMakeLists.txt') -PathType Leaf) { return $true }
    return (Test-Path -LiteralPath (Join-Path (Join-Path $Folder 'sensors') 'XRPerfSensors.csproj') -PathType Leaf)
}

# $true si $Folder est le dossier des sources ou contient le dossier compilé : y installer
# mélangerait le programme aux sources et remplacerait leur sous-dossier sensors\.
function Test-ProjectFolder([string]$Folder, [string]$SourceFolder) {
    if (Test-HasSources $Folder) { return $true }
    $prefix = $Folder.TrimEnd([System.IO.Path]::DirectorySeparatorChar) + [System.IO.Path]::DirectorySeparatorChar
    return $SourceFolder.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)
}

# Installation faite à la main et pas (ou plus) enregistrée : on la reconnaît à la DLL, dans le
# dossier par défaut ou dans son sous-dossier "dist". Retourne le dossier, ou une chaîne vide.
function Find-UnregisteredFolder([string]$DefaultFolder) {
    foreach ($candidate in @($DefaultFolder, (Join-Path $DefaultFolder 'dist'))) {
        if (Test-Path -LiteralPath (Join-Path $candidate $LayerDll) -PathType Leaf) { return $candidate }
    }
    return ''
}

# Liste ce qui manque dans le dossier compilé (liste vide = complet).
function Get-MissingSourceFiles([string]$SourceFolder) {
    $required = @($LayerDll, $LayerJson, $IniName) + $ScriptFiles
    foreach ($name in @('XRPerfSensors.exe', 'XRPerfSensors.dll', 'XRPerfSensors.deps.json', 'XRPerfSensors.runtimeconfig.json')) {
        $required += (Join-Path 'sensors' $name)
    }
    $missing = @()
    foreach ($relative in $required) {
        if (-not (Test-Path -LiteralPath (Join-Path $SourceFolder $relative) -PathType Leaf)) { $missing += $relative }
    }
    # La bibliothèque de lecture des capteurs n'est pas à côté du programme : dotnet la range dans
    # un sous-dossier propre au système (sensors\runtimes\win-x64\lib\...). On la cherche donc partout.
    $sensorsFolder = Join-Path $SourceFolder 'sensors'
    if (Test-Path -LiteralPath $sensorsFolder -PathType Container) {
        $library = @(Get-ChildItem -LiteralPath $sensorsFolder -Recurse -File -Filter 'LibreHardwareMonitorLib.dll' -ErrorAction SilentlyContinue)
        if ($library.Count -eq 0) { $missing += (Join-Path 'sensors' 'LibreHardwareMonitorLib.dll (introuvable dans tout le dossier)') }
    }
    return , $missing
}

# Vérifie, sans rien modifier, que le sous-dossier sensors\ du dossier d'installation peut être
# remplacé : c'est bien le nôtre, et ce n'est pas un lien vers un autre dossier.
function Assert-SensorsFolderReplaceable([string]$TargetFolder) {
    $targetSensors = Join-Path $TargetFolder 'sensors'
    if (-not (Test-Path -LiteralPath $targetSensors)) { return }
    $item = Get-Item -LiteralPath $targetSensors -Force
    if (-not $item.PSIsContainer -or ($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint)) {
        throw "Dans le dossier d'installation, « sensors » n'est pas un dossier ordinaire (fichier ou lien) : $targetSensors. Choisissez un autre dossier avec l'option -Target. Rien n'a été modifié."
    }
    $ours = (Test-Path -LiteralPath (Join-Path $TargetFolder $LayerDll) -PathType Leaf) -or
            (Test-Path -LiteralPath (Join-Path $TargetFolder $LayerJson) -PathType Leaf) -or
            (Test-Path -LiteralPath (Join-Path $targetSensors 'XRPerfSensors.exe') -PathType Leaf)
    if (-not $ours) {
        throw "Le dossier choisi contient déjà un sous-dossier « sensors » qui ne vient pas de XR Perf Overlay : $targetSensors. Choisissez un dossier vide ou une installation existante. Rien n'a été modifié."
    }
}

# Copie le dossier compilé vers le dossier d'installation. Les réglages et les résumés de session
# déjà présents sont conservés. Retourne $true si un fichier de réglages existant a été gardé.
function Install-Files([string]$SourceFolder, [string]$TargetFolder) {
    if (-not (Test-Path -LiteralPath $TargetFolder -PathType Container)) {
        New-Item -ItemType Directory -Path $TargetFolder -Force | Out-Null
    }

    # Programme de capteurs : le sous-dossier est remplacé en entier, pour qu'aucun fichier d'une
    # ancienne version ne traîne (ses propres réglages sont ailleurs, dans %LOCALAPPDATA%). La
    # nouvelle version est d'abord copiée à côté, puis les dossiers sont renommés : si l'ancien ne
    # peut pas être écarté (fichier encore ouvert), l'erreur survient avant tout changement.
    $targetSensors = Join-Path $TargetFolder 'sensors'
    $newSensors = Join-Path $TargetFolder 'sensors.nouveau'
    $oldSensors = Join-Path $TargetFolder 'sensors.ancien'
    foreach ($leftover in @($newSensors, $oldSensors)) {
        if (Test-Path -LiteralPath $leftover) { Remove-Item -LiteralPath $leftover -Recurse -Force }
    }
    Copy-Item -LiteralPath (Join-Path $SourceFolder 'sensors') -Destination $newSensors -Recurse -Force
    if (Test-Path -LiteralPath $targetSensors) { Rename-Item -LiteralPath $targetSensors -NewName 'sensors.ancien' }
    Rename-Item -LiteralPath $newSensors -NewName 'sensors'

    foreach ($name in (@($LayerDll, $LayerJson) + $ScriptFiles)) {
        Copy-Item -LiteralPath (Join-Path $SourceFolder $name) -Destination (Join-Path $TargetFolder $name) -Force
    }
    # Numéro de version, pour savoir ce qui est installé (absent des compilations anciennes).
    $versionFile = Join-Path $SourceFolder 'VERSION'
    if (Test-Path -LiteralPath $versionFile -PathType Leaf) {
        Copy-Item -LiteralPath $versionFile -Destination (Join-Path $TargetFolder 'VERSION') -Force
    }

    # Réglages : ceux de l'utilisateur ne sont jamais écrasés. Le modèle à jour est posé à côté,
    # pour retrouver les options ajoutées par une nouvelle version.
    $targetIni = Join-Path $TargetFolder $IniName
    $keptIni = Test-Path -LiteralPath $targetIni -PathType Leaf
    if ($keptIni) {
        Copy-Item -LiteralPath (Join-Path $SourceFolder $IniName) -Destination (Join-Path $TargetFolder $IniTemplateName) -Force
    } else {
        Copy-Item -LiteralPath (Join-Path $SourceFolder $IniName) -Destination $targetIni -Force
    }

    Unblock-Installed $TargetFolder (@($LayerDll, $LayerJson, 'VERSION') + $ScriptFiles)

    # L'ancienne version du programme de capteurs n'est plus utilisée : son retrait peut échouer
    # sans conséquence (il sera refait à la prochaine installation).
    if (Test-Path -LiteralPath $oldSensors) {
        try {
            Remove-Item -LiteralPath $oldSensors -Recurse -Force
        } catch {
            Write-Host "  (l'ancien dossier $oldSensors n'a pas pu être supprimé ; vous pouvez l'effacer à la main)"
        }
    }
    return $keptIni
}

# Numéro de version du dossier à installer (fichier VERSION), ou chaîne vide.
function Get-SourceVersion([string]$SourceFolder) {
    $versionFile = Join-Path $SourceFolder 'VERSION'
    if (-not (Test-Path -LiteralPath $versionFile -PathType Leaf)) { return '' }
    $line = Get-Content -LiteralPath $versionFile -TotalCount 1 -ErrorAction SilentlyContinue
    if ($null -eq $line) { return '' }
    return ([string]$line).Trim()
}

# --- Déroulement ---------------------------------------------------------------------------------

# Relance ce script avec les droits administrateur (invite Windows), dans une nouvelle fenêtre.
function Start-Elevated([string]$SourceFolder) {
    # Un lecteur réseau n'existe pas pour le compte administrateur : la nouvelle fenêtre ne
    # trouverait pas ce script et se refermerait sans explication.
    $onNetworkDrive = $false
    try {
        $drive = New-Object System.IO.DriveInfo ([System.IO.Path]::GetPathRoot($PSCommandPath))
        $onNetworkDrive = ($drive.DriveType -eq [System.IO.DriveType]::Network)
    } catch { }
    if ($onNetworkDrive) {
        throw "Le dossier extrait se trouve sur un lecteur réseau. Copiez-le sur un disque de ce PC (Téléchargements, par exemple), puis relancez Installer.bat."
    }

    # Le nom du compte est relevé ici, avant l'élévation : si l'invite est validée avec un autre
    # compte administrateur, le démarrage automatique doit rester celui de l'utilisateur.
    $account = "$env:USERDOMAIN\$env:USERNAME"
    $arguments = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$PSCommandPath`"", '-Elevated',
                   '-Source', "`"$SourceFolder`"", '-User', "`"$account`"")
    if ($Target) { $arguments += @('-Target', "`"$(Get-FullPath $Target)`"") }
    if ($AutoStart) { $arguments += @('-AutoStart', $AutoStart) }
    try {
        Start-Process powershell -Verb RunAs -ArgumentList $arguments
    } catch {
        Write-Host "Installation annulée : l'autorisation d'administrateur a été refusée ou n'a pas pu être demandée." -ForegroundColor Yellow
        Write-Host "  ($($_.Exception.Message))"
        Write-Host "Elle est nécessaire pour enregistrer la layer auprès d'OpenXR. Relancez Installer.bat pour réessayer."
        return $false
    }
    $script:Relaunched = $true
    Write-Host "L'installation continue dans la nouvelle fenêtre."
    return $true
}

function Read-AutoStartChoice {
    if ($AutoStart -match '^(oui|o|yes|y|1)$') { return $true }
    if ($AutoStart -match '^(non|n|no|0)$') { return $false }
    if (Test-AutoStartTask) { return $true }  # déjà choisi lors d'une installation précédente
    Write-Host ''
    Write-Host 'XRPerfSensors fournit les températures, charges, fréquences et la VRAM au panneau.'
    $answer = Read-Host 'Le lancer automatiquement à chaque ouverture de session Windows ? [O/n]'
    return -not ($answer -match '^\s*n')
}

# Choisit le dossier d'installation. Retourne @{ Folder ; Existing } (Existing : installation trouvée).
function Resolve-TargetFolder([string]$SourceFolder) {
    $systemDrive = $env:SystemDrive
    if (-not $systemDrive) { $systemDrive = 'C:' }
    $defaultFolder = Join-Path $systemDrive 'XRPerfOverlay'

    # Au même endroit qu'une installation existante, pour garder réglages et résumés.
    $existing = Get-RegisteredFolder
    if (-not $existing) { $existing = Find-UnregisteredFolder $defaultFolder }
    if ($existing) { $existing = Get-FullPath $existing }

    if ($Target) {
        $folder = Get-FullPath $Target
        if ($folder.Length -le 3) { throw "Le dossier d'installation ne peut pas être la racine d'un disque ($folder) : indiquez un sous-dossier." }
        if (-not (Test-SamePath $folder $SourceFolder) -and (Test-ProjectFolder $folder $SourceFolder)) {
            throw "Le dossier choisi ($folder) est celui des sources de XR Perf Overlay, ou le contient : y installer mélangerait le programme aux fichiers nécessaires à la compilation. Choisissez un autre dossier. Rien n'a été modifié."
        }
    } elseif ($existing) {
        $folder = $existing
    } else {
        $folder = $defaultFolder
    }
    # L'archive a été extraite là où l'installation irait par défaut (C:\XRPerfOverlay) : le programme
    # est alors rangé dans son sous-dossier dist, à l'écart des sources.
    if (-not $Target -and -not (Test-SamePath $folder $SourceFolder) -and (Test-ProjectFolder $folder $SourceFolder)) {
        $folder = Join-Path $folder 'dist'
    }
    return @{ Folder = $folder; Existing = $existing }
}

# Retourne $true si l'installation est allée au bout.
function Invoke-Setup {
    if (-not $Source) { throw "Dossier compilé non précisé (option -Source). Lancez Installer.bat plutôt que ce script." }
    if (-not (Test-Path -LiteralPath $Source -PathType Container)) { throw "Dossier compilé introuvable : $Source" }
    $sourceFolder = Get-FullPath $Source
    $missing = Get-MissingSourceFiles $sourceFolder
    if ($missing.Count -gt 0) {
        throw ("Le dossier compilé est incomplet ($sourceFolder). Il manque : " + ($missing -join ', ') +
               ". Relancez Installer.bat et regardez build.log si la compilation échoue.")
    }

    if (-not (Test-IsAdmin)) {
        if ($Elevated) { throw "Les droits administrateur n'ont pas été obtenus." }
        return (Start-Elevated $sourceFolder)
    }

    $current = "$env:USERDOMAIN\$env:USERNAME"
    $account = $User
    if (-not $account) { $account = $current }

    $resolved = Resolve-TargetFolder $sourceFolder
    $targetFolder = $resolved.Folder
    $existing = $resolved.Existing
    $inPlace = Test-SamePath $targetFolder $sourceFolder
    $insideProject = Test-HasSources (Split-Path -Parent $targetFolder)

    $version = Get-SourceVersion $sourceFolder
    Write-Host ''
    if ($version) { Write-Host "XR Perf Overlay $version" }
    if ($existing) { Write-Host "Installation existante trouvée : $existing" }
    Write-Host "Dossier d'installation : $targetFolder"

    $withAutoStart = Read-AutoStartChoice

    # Dernières vérifications avant de toucher à quoi que ce soit.
    if (-not $inPlace) { Assert-SensorsFolderReplaceable $targetFolder }

    Write-Host ''
    Write-Host 'Arrêt de XRPerfSensors...'
    $sensorsWereRunning = [bool](Stop-Sensors)
    if (-not $inPlace) {
        $installedSensors = Join-Path (Join-Path $targetFolder 'sensors') 'XRPerfSensors.exe'
        if (-not (Wait-FileUnlocked $installedSensors)) {
            throw "XRPerfSensors ne s'est pas arrêté. Quittez-le (clic droit sur son icône, près de l'horloge, puis Quitter) et relancez Installer.bat. Aucun fichier n'a été modifié."
        }
        # XRPerfSensors charge lui aussi la DLL (pour son aperçu) : le contrôle se fait donc après son
        # arrêt. Si elle reste utilisée, c'est qu'un jeu OpenXR encore ouvert la garde chargée.
        if (-not (Wait-FileUnlocked (Join-Path $targetFolder $LayerDll) 10)) {
            if ($sensorsWereRunning) { Restart-Sensors $targetFolder }
            throw "La DLL de l'overlay ne peut pas être remplacée : un jeu ou un programme VR l'utilise encore (ou le fichier est protégé en écriture). Fermez le jeu, ainsi que SteamVR ou le logiciel du casque s'il reste ouvert, puis relancez Installer.bat. Rien n'a été modifié."
        }
    }

    $keptIni = $false
    $carriedIni = $false
    try {
        if ($inPlace) {
            Write-Host 'Les fichiers compilés sont déjà dans le dossier d''installation : aucune copie.'
        } else {
            # Nouveau dossier choisi alors qu'une installation existe ailleurs : ses réglages suivent.
            $targetIni = Join-Path $targetFolder $IniName
            if ($existing -and -not (Test-SamePath $existing $targetFolder) -and -not (Test-Path -LiteralPath $targetIni)) {
                $existingIni = Join-Path $existing $IniName
                if (Test-Path -LiteralPath $existingIni -PathType Leaf) {
                    if (-not (Test-Path -LiteralPath $targetFolder -PathType Container)) { New-Item -ItemType Directory -Path $targetFolder -Force | Out-Null }
                    Copy-Item -LiteralPath $existingIni -Destination $targetIni -Force
                    $carriedIni = $true
                }
            }
            Write-Host 'Copie des fichiers...'
            $keptIni = Install-Files $sourceFolder $targetFolder
        }

        Write-Host 'Enregistrement auprès d''OpenXR...'
        Register-Layer $targetFolder $withAutoStart $account | Out-Null
        if (-not $withAutoStart) {
            Write-Host 'Lancement de XRPerfSensors...'
            Start-Sensors $targetFolder | Out-Null
        }
    } catch {
        throw ("$($_.Exception.Message)`n`nL'installation n'est pas terminée et XRPerfSensors est arrêté. Vos réglages et vos résumés de session ne sont pas touchés. " +
               "Corrigez la cause indiquée ci-dessus puis relancez Installer.bat : il reprendra proprement.")
    }
    $sensorsRunning = Test-SensorsRunning

    Write-Host ''
    Write-Host 'Installation terminée.' -ForegroundColor Green
    if ($version) { Write-Host "  Version        : $version" }
    Write-Host "  Dossier        : $targetFolder"
    if ($insideProject) {
        Write-Host '                   (il se trouve dans un dossier de sources extrait de l''archive : ne supprimez pas ce dossier)'
    }
    if ($carriedIni) {
        Write-Host "  Réglages       : repris de l'ancienne installation ($existing) ; $IniTemplateName montre les options de cette version."
        Write-Host "                   Les résumés de session déjà écrits restent dans l'ancien dossier."
    } elseif ($keptIni) {
        Write-Host "  Réglages       : votre $IniName a été conservé ; $IniTemplateName montre les options de cette version."
    } else {
        Write-Host "  Réglages       : $IniName (à modifier avec le Bloc-notes)."
    }
    if (-not $sensorsRunning) {
        Write-Host "  Capteurs       : XRPerfSensors n'a pas démarré. Lancez sensors\XRPerfSensors.exe depuis le dossier d'installation." -ForegroundColor Yellow
    } elseif ($withAutoStart) {
        Write-Host '  Capteurs       : XRPerfSensors est lancé et démarrera avec Windows.'
    } else {
        Write-Host '  Capteurs       : XRPerfSensors est lancé ; à relancer vous-même après un redémarrage (sensors\XRPerfSensors.exe).'
    }
    if ($withAutoStart -and -not (Test-SamePath $account $current)) {
        Write-Host "                   Attention : l'autorisation a été donnée avec un autre compte ($current) que le vôtre ($account)." -ForegroundColor Yellow
        Write-Host "                   Le démarrage automatique ne fonctionne que si votre compte est administrateur de ce PC." -ForegroundColor Yellow
    }
    Write-Host '  Dans le casque : Ctrl+Alt+O affiche ou masque le panneau.'
    Write-Host "  Désinstaller   : uninstall.bat dans le dossier d'installation."
    return $true
}

# Codes de sortie lus par Installer.bat : 0 = terminé, 3 = arrêt expliqué à l'écran (pause déjà faite).
# Tout autre code signifie que PowerShell n'a pas pu exécuter ce script.
function Invoke-Main {
    $ok = $false
    try {
        $ok = [bool](Invoke-Setup)
    } catch {
        Write-Host ''
        Write-Host "ERREUR : $($_.Exception.Message)" -ForegroundColor Red
    }
    # On laisse le temps de lire le résultat, sauf dans la fenêtre de départ quand l'installation se
    # poursuit dans une autre (celle ouverte par la demande d'autorisation).
    if (-not $script:Relaunched) { Read-Host 'Appuyez sur Entrée pour fermer' | Out-Null }
    if ($ok) { exit 0 } else { exit 3 }
}

# Chargé avec ". setup.ps1" (tests), le script ne fait que définir ses fonctions.
if ($MyInvocation.InvocationName -ne '.') { Invoke-Main }
