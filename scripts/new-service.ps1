<#
    new-service.ps1 — Amorce un nouveau service morfSystem a partir de ce template.

    Copie tout le projet dans un nouveau dossier en remplacant les noms :
        morfMonitor  -> <NomCamel>   (projet / bibliotheque / cible CMake)
        morfmonitor         -> <nom>        (namespace, include, binaire, config)
        MORFMONITOR         -> <NOM>        (prefixe de macro)

    Usage :
        powershell -ExecutionPolicy Bypass -File scripts\new-service.ps1 <nom> <NomCamel> [dest]
    Exemple :
        ... new-service.ps1 morfwatch morfWatch
          -> cree ..\morfWatch_travail
#>
param(
    [Parameter(Mandatory=$true)][string]$Lower,
    [Parameter(Mandatory=$true)][string]$Camel,
    [string]$Dest = ""
)
$ErrorActionPreference = "Stop"
$Upper = $Lower.ToUpper()

$root = Split-Path -Parent $PSScriptRoot
if (-not $Dest) { $Dest = Join-Path (Split-Path -Parent $root) "${Camel}_travail" }
if (Test-Path $Dest) { throw "Destination deja existante : $Dest" }

Write-Host "Template : $root"
Write-Host "Nouveau  : $Dest  ($Camel / $Lower / $Upper)"

# --- 1. Copier l'arbre (sans .git ni build) ------------------------------
New-Item -ItemType Directory -Force -Path $Dest | Out-Null
Get-ChildItem -Path $root -Force | Where-Object {
    $_.Name -notin @('.git','build') -and $_.Name -notlike 'build-*'
} | Copy-Item -Destination $Dest -Recurse -Force

# --- 2. Remplacer les jetons dans les fichiers texte (hors third_party) --
$exts = @('*.cpp','*.h','*.txt','*.json','*.md','*.sh','*.ps1','*.service','*.gitignore','*.gitattributes','VERSION','CMakePresets.json')
Get-ChildItem -Path $Dest -Recurse -File | Where-Object {
    $_.FullName -notmatch '\\third_party\\morf\\beacon\\'   # ne pas toucher au code vendore
} | ForEach-Object {
    try {
        $c = Get-Content -LiteralPath $_.FullName -Raw -ErrorAction Stop
    } catch { return }
    if ($c -match 'morfMonitor|morfmonitor|MORFMONITOR') {
        $c = $c -creplace 'morfMonitor', $Camel `
                -creplace 'morfmonitor',        $Lower `
                -creplace 'MORFMONITOR',         $Upper
        Set-Content -LiteralPath $_.FullName -Value $c -NoNewline
    }
}

# --- 3. Renommer fichiers/dossiers portant l'ancien nom ------------------
Rename-Item "$Dest\include\morfmonitor"               $Lower
Rename-Item "$Dest\config\morfmonitor.example.json"   "$Lower.example.json"
Rename-Item "$Dest\scripts\linux\morfmonitor.service" "$Lower.service"

Write-Host "Termine."
Write-Host "  cd `"$Dest`""
Write-Host "  cmake --preset mingw ; cmake --build --preset mingw"
Write-Host "  puis coder le metier dans src\ExampleModule.* et adapter ModuleFactory / CMakeLists."
