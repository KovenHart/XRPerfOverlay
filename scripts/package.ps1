# Construit le paquet de distribution de XR Perf Overlay : le programme déjà compilé, sans les
# sources, prêt à être installé par un double-clic sur Installer.bat (sans Visual Studio ni SDK .NET).
#
#   release\XRPerfOverlay-<version>\              dossier du paquet
#   release\XRPerfOverlay-<version>-win64.zip     le même, compressé (à publier)
#   release\XRPerfOverlay-<version>-win64.zip.sha256
#
# La layer doit déjà être compilée (build.bat, ou CMake dans la compilation automatique de GitHub).
# XRPerfSensors est publié ici en mode autonome : le runtime .NET est inclus, l'utilisateur n'a rien
# à installer. Compatible avec Windows PowerShell 5.1 et PowerShell 7.
#
# Contenu du paquet :
#   Installer.bat           installation ou mise à jour (mode « paquet » : rien à compiler)
#   LISEZ-MOI.txt           guide de l'utilisateur (README-EN.txt : le même en anglais)
#   README.md, README.en.md, docs\   documentation complète, en français et en anglais
#   LICENSE.txt, THIRD-PARTY.md
#   scripts\setup.ps1       script d'installation lancé par Installer.bat
#   programme\              ce qui est copié dans le dossier d'installation (XRPerfVR et XRPerfCapture dans programme\sensors)
param(
    # DLL de la layer compilée en Release.
    [string]$LayerDll = '',
    # Dossier de sortie (relatif à la racine du projet ou complet).
    [string]$Out = 'release',
    # Commande dotnet à utiliser (les essais la remplacent par une doublure).
    [string]$Dotnet = 'dotnet'
)

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
$LayerName = 'XR_APILAYER_NOVENDOR_perf_overlay'

# Chemin sous la racine du projet, élément par élément (portable).
function Under([string[]]$Parts) {
    $path = $Root
    foreach ($part in $Parts) { $path = Join-Path $path $part }
    return $path
}

function Resolve-FromRoot([string]$Path) {
    if ([System.IO.Path]::IsPathRooted($Path)) { return [System.IO.Path]::GetFullPath($Path) }
    return [System.IO.Path]::GetFullPath((Join-Path $Root $Path))
}

function Copy-Required([string]$From, [string]$To) {
    if (-not (Test-Path -LiteralPath $From -PathType Leaf)) { throw "Fichier introuvable : $From" }
    $folder = Split-Path -Parent $To
    if (-not (Test-Path -LiteralPath $folder -PathType Container)) { New-Item -ItemType Directory -Path $folder -Force | Out-Null }
    Copy-Item -LiteralPath $From -Destination $To -Force
}

# --- Version ---------------------------------------------------------------------------------------
$versionFile = Join-Path $Root 'VERSION'
if (-not (Test-Path -LiteralPath $versionFile -PathType Leaf)) { throw "Fichier VERSION introuvable à la racine du projet ($Root)." }
$version = ([string](Get-Content -LiteralPath $versionFile -TotalCount 1)).Trim()
if ($version -notmatch '^\d+\.\d+\.\d+$') { throw "Le fichier VERSION doit contenir un numéro de la forme 1.2.3 (lu : « $version »)." }
$name = "XRPerfOverlay-$version"
Write-Host "Paquet de XR Perf Overlay $version"

# --- Dossier du paquet -----------------------------------------------------------------------------
if (-not $LayerDll) { $LayerDll = Join-Path (Join-Path (Join-Path 'build' 'layer') 'Release') "$LayerName.dll" }
$layerPath = Resolve-FromRoot $LayerDll
if (-not (Test-Path -LiteralPath $layerPath -PathType Leaf)) {
    throw "DLL de la layer introuvable : $layerPath. Compilez-la d'abord (build.bat) ou indiquez-la avec -LayerDll."
}
# La DLL doit venir de cette version : sa ressource de version est lue quand le système le permet
# (sous Windows ; ailleurs, le champ reste vide et la vérification est sautée).
$dllVersion = ''
try { $dllVersion = [string][System.Diagnostics.FileVersionInfo]::GetVersionInfo($layerPath).ProductVersion } catch { }
if ($dllVersion -and $dllVersion.Trim() -ne $version) {
    throw "La DLL $layerPath porte la version « $dllVersion », pas $version : recompilez-la (build.bat) après avoir changé le fichier VERSION."
}

$outFolder = Resolve-FromRoot $Out
$staging = Join-Path $outFolder $name
$program = Join-Path $staging 'programme'
if (Test-Path -LiteralPath $staging) { Remove-Item -LiteralPath $staging -Recurse -Force }
New-Item -ItemType Directory -Path $program -Force | Out-Null

Write-Host 'Layer et fichiers de configuration...'
Copy-Required $layerPath (Join-Path $program "$LayerName.dll")
Copy-Required (Under 'layer', "$LayerName.json") (Join-Path $program "$LayerName.json")
Copy-Required (Under 'layer', 'XRPerfOverlay.ini') (Join-Path $program 'XRPerfOverlay.ini')
Copy-Required $versionFile (Join-Path $program 'VERSION')
foreach ($script in @('install.ps1', 'install.bat', 'uninstall.ps1', 'uninstall.bat')) {
    Copy-Required (Under 'scripts', $script) (Join-Path $program $script)
}

Write-Host 'XRPerfSensors (publication autonome, runtime .NET inclus)...'
$project = Under 'sensors', 'XRPerfSensors.csproj'
& $Dotnet publish $project -c Release -r win-x64 --self-contained true -o (Join-Path $program 'sensors') --nologo `
    '-p:DebugType=None' '-p:DebugSymbols=false'
if ($LASTEXITCODE -ne 0) { throw "La publication de XRPerfSensors a échoué (dotnet, code $LASTEXITCODE)." }
# Une publication autonome se reconnaît à son fichier runtimeconfig : il cite les composants .NET
# inclus (« includedFrameworks ») au lieu de ceux à trouver sur le PC (« frameworks »).
$runtimeConfig = Join-Path (Join-Path $program 'sensors') 'XRPerfSensors.runtimeconfig.json'
if (-not (Test-Path -LiteralPath $runtimeConfig -PathType Leaf) -or -not ((Get-Content -LiteralPath $runtimeConfig -Raw) -match 'includedFrameworks')) {
    throw "XRPerfSensors n'a pas été publié en mode autonome ($runtimeConfig) : le paquet demanderait d'installer .NET."
}

# XRPerfVR (jeux SteamVR / OpenVR) et la bibliothèque de Valve qu'il utilise : compilés avec la layer
# (même dossier), rangés à côté de XRPerfSensors qui les lance.
Write-Host 'XRPerfVR (jeux SteamVR / OpenVR)...'
$layerFolder = Split-Path -Parent $layerPath
# XRPerfCapture.dll (fenêtre du bureau affichée dans le casque), chargée par XRPerfSensors, est rangée au même endroit.
foreach ($file in @('XRPerfVR.exe', 'openvr_api.dll', 'XRPerfCapture.dll')) {
    Copy-Required (Join-Path $layerFolder $file) (Join-Path (Join-Path $program 'sensors') $file)
}

Write-Host 'Installateur et documentation...'
Copy-Required (Under 'Installer.bat') (Join-Path $staging 'Installer.bat')
Copy-Required (Under 'scripts', 'setup.ps1') (Join-Path (Join-Path $staging 'scripts') 'setup.ps1')
Copy-Required (Under 'docs', 'LISEZ-MOI.txt') (Join-Path $staging 'LISEZ-MOI.txt')
Copy-Required (Under 'docs', 'README-EN.txt') (Join-Path $staging 'README-EN.txt')
Copy-Required (Join-Path $Root 'README.md') (Join-Path $staging 'README.md')
Copy-Required (Join-Path $Root 'README.en.md') (Join-Path $staging 'README.en.md')
Copy-Required (Join-Path $Root 'LICENSE') (Join-Path $staging 'LICENSE.txt')
Copy-Required (Join-Path $Root 'THIRD-PARTY.md') (Join-Path $staging 'THIRD-PARTY.md')
foreach ($image in @(Get-ChildItem -LiteralPath (Under 'docs') -Filter '*.png')) {
    Copy-Required $image.FullName (Join-Path (Join-Path $staging 'docs') $image.Name)
}

# --- Contrôle : le script d'installation doit trouver tout ce qu'il attend --------------------------
# Chargé dans une portée à part : ses variables ne se mêlent pas à celles de ce script.
$setupScript = Under 'scripts', 'setup.ps1'
$missing = & { param($SetupPath, $Folder) . $SetupPath; Get-MissingSourceFiles $Folder } $setupScript $program
if ($missing.Count -gt 0) { throw ("Paquet incomplet, il manque : " + ($missing -join ', ')) }

# --- Archive ---------------------------------------------------------------------------------------
$zip = Join-Path $outFolder "$name-win64.zip"
if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
# Le dossier XRPerfOverlay-<version> est inclus : l'archive s'extrait dans un dossier à son nom.
[System.IO.Compression.ZipFile]::CreateFromDirectory($staging, $zip, [System.IO.Compression.CompressionLevel]::Optimal, $true)
$hash = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant()
[System.IO.File]::WriteAllText("$zip.sha256", "$hash  $name-win64.zip`n")

$size = (Get-Item -LiteralPath $zip).Length / 1MB
Write-Host ''
Write-Host ("Paquet prêt : {0} ({1:N1} Mo)" -f $zip, $size) -ForegroundColor Green
Write-Host "SHA-256     : $hash"
