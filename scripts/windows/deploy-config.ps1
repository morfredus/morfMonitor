<#
.SYNOPSIS
    Copie la configuration du depot vers le service installe (Windows).

.DESCRIPTION
    Equivalent Windows de scripts/linux/deploy-config.sh.

    Version DIRECTE : ecrase la configuration deployee par celle du depot.
    Pas de fusion, pas de question.

        depot                                  service installe
        config\morfmonitor.json         --->   %ProgramData%\morfmonitor\morfmonitor.json
        (a defaut : morfmonitor.example.json)

    La configuration en place est sauvegardee (.bak-<date>) avant d'etre
    remplacee : si elle portait des reglages propres a la machine, ils sont dans
    la sauvegarde.

    Pour n'ajouter que les cles nouvelles sans toucher a l'existant, utiliser
    plutot update-service.ps1 (qui appelle merge-config.py).

.EXAMPLE
    .\scripts\windows\deploy-config.ps1
    .\scripts\windows\deploy-config.ps1 -NoRestart
    .\scripts\windows\deploy-config.ps1 -AppDir "D:\services\morfmonitor"
#>
param(
    [string]$AppDir = "$env:ProgramData\morfmonitor",
    [switch]$NoRestart
)

$ErrorActionPreference = "Stop"
$TaskName = "morfmonitor"
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)

# Source : la configuration reelle du depot si elle existe, sinon l'exemple.
$src = Join-Path $repoRoot "config\morfmonitor.json"
if (-not (Test-Path -LiteralPath $src)) {
    $src = Join-Path $repoRoot "config\morfmonitor.example.json"
}
if (-not (Test-Path -LiteralPath $src)) {
    throw "Aucune configuration source dans $repoRoot\config\"
}

if (-not (Test-Path -LiteralPath $AppDir)) {
    throw "Service non installe : $AppDir absent.`nLancer d'abord : .\scripts\windows\install-service.ps1"
}

$dest = Join-Path $AppDir "morfmonitor.json"
Write-Host "Source      : $src"
Write-Host "Destination : $dest"

if (Test-Path -LiteralPath $dest) {
    $backup = "$dest.bak-$(Get-Date -Format 'yyyyMMdd-HHmmss')"
    Copy-Item -LiteralPath $dest -Destination $backup
    Write-Host "Sauvegarde  : $backup"

    # Apercu plafonne : voir d'un coup d'oeil ce qui change, pas relire les
    # deux fichiers.
    $a = Get-Content -LiteralPath $backup
    $b = Get-Content -LiteralPath $src
    $diff = Compare-Object $a $b
    if ($diff) {
        Write-Host ""
        Write-Host "Differences appliquees :"
        $diff | Select-Object -First 30 | ForEach-Object {
            $sign = if ($_.SideIndicator -eq "=>") { "+" } else { "-" }
            Write-Host ("    {0} {1}" -f $sign, $_.InputObject)
        }
        if ($diff.Count -gt 30) {
            Write-Host ("    ... ({0} lignes de plus)" -f ($diff.Count - 30))
        }
        Write-Host ""
    }
}

# Windows n'a pas d'equivalent de sudo : un script ne peut pas elever une seule
# ecriture. Plutot que d'exiger l'administrateur d'emblee -- inutile quand
# -AppDir pointe vers un dossier accessible -- on tente l'ecriture et on explique
# precisement l'echec. Un « acces refuse » brut enverrait chercher un probleme
# de fichier la ou il s'agit de droits.
try {
    Copy-Item -LiteralPath $src -Destination $dest -Force -ErrorAction Stop
} catch [System.UnauthorizedAccessException] {
    throw "Ecriture refusee : $dest`nRelancer dans une PowerShell Administrateur, ou viser un dossier accessible avec -AppDir."
}
Write-Host "Configuration copiee."

if (-not $NoRestart) {
    schtasks /End /TN $TaskName 2>$null | Out-Null
    Start-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 1
    Write-Host ""
    Write-Host "Service redemarre."
    Write-Host "Verifier :  curl http://127.0.0.1:8790/api/all"
    Write-Host "Etat     :  schtasks /Query /TN $TaskName"
} else {
    Write-Host "Redemarrer pour appliquer :  schtasks /End /TN $TaskName ; Start-ScheduledTask -TaskName $TaskName"
}
