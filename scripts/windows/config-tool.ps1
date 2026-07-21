<#
.SYNOPSIS
    Gestion de la configuration deployee de morfMonitor (Windows).

.DESCRIPTION
    Equivalent Windows de scripts/linux/config-tool.sh.

    L'installation et la mise a jour ne remplacent jamais morfmonitor.json : il
    porte des reglages locaux irrecuperables. Mais merge-config.py n'ajoute que
    les cles MANQUANTES ; il ne corrige pas une valeur DEJA PRESENTE devenue
    invalide. Un module dont le type a disparu de la fabrique reste en place, et
    le service demarre sans rien superviser.

    Reconcilier une valeur existante ne peut pas etre automatique : seul
    l'utilisateur sait si une valeur est un reglage voulu ou un residu. Cet
    outil rend l'operation explicite, et toute ecriture est precedee d'une
    sauvegarde datee.

    La logique JSON (fusion, verification) vit dans les scripts Python de
    scripts/linux/, appeles ici tels quels. Python est le SEUL des trois
    langages de cet ecosysteme qui tourne a l'identique sur Windows, Linux et
    Raspberry Pi : le reimplementer en PowerShell creerait deux verificateurs
    libres de se contredire, sur le fichier qui decide si le service fonctionne.
    Meme raison que morfTools/scripts/ecosystem-check.py, partage par morf.sh et
    morf.ps1.

.PARAMETER Action
    status | check | diff | merge | reset

.EXAMPLE
    .\scripts\windows\config-tool.ps1 status
    .\scripts\windows\config-tool.ps1 diff
    .\scripts\windows\config-tool.ps1 merge
    .\scripts\windows\config-tool.ps1 reset
#>
param(
    [Parameter(Position = 0)]
    [ValidateSet('status', 'check', 'diff', 'merge', 'reset')]
    [string]$Action = 'status',

    [string]$AppDir = "$env:ProgramData\morfmonitor",
    [switch]$Yes
)

$ErrorActionPreference = "Stop"
$TaskName = "morfmonitor"
$repoRoot   = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$linuxDir   = Join-Path $repoRoot "scripts\linux"     # les .py y vivent
$configFile = Join-Path $AppDir  "morfmonitor.json"
$example    = Join-Path $repoRoot "config\morfmonitor.example.json"
$binary     = Join-Path $AppDir  "morfmonitor.exe"

function Get-Python {
    foreach ($c in @('python', 'python3', 'py')) {
        $cmd = Get-Command $c -ErrorAction SilentlyContinue
        if ($cmd) { return $cmd.Source }
    }
    throw "Python introuvable. Les verifications de configuration en dependent.`nInstaller Python, ou utiliser deploy-config.ps1 qui n'en a pas besoin."
}

function Assert-Config {
    if (-not (Test-Path -LiteralPath $configFile)) {
        throw "Configuration absente : $configFile`nInstaller le service d'abord : .\scripts\windows\install-service.ps1"
    }
}

# Sauvegarde datee avant toute ecriture. Jamais d'exception : une configuration
# perdue ne se reconstitue pas.
function New-Backup {
    $backup = "$configFile.bak-$(Get-Date -Format 'yyyyMMdd-HHmmss')"
    Copy-Item -LiteralPath $configFile -Destination $backup
    return $backup
}

function Invoke-Check {
    $py = Get-Python
    $args = @((Join-Path $linuxDir "check-config.py"), $configFile)
    if (Test-Path -LiteralPath $binary)  { $args += @('--binary',  $binary) }
    if (Test-Path -LiteralPath $example) { $args += @('--example', $example) }
    $args += @('--hint-style', 'ps1')
    # Out-Host, et non le flux de sortie : une fonction PowerShell renvoie TOUT
    # ce qu'elle ecrit. Sans cela, les lignes du diagnostic se melangent au code
    # de retour et l'appelant recoit un tableau au lieu d'un entier.
    & $py @args | Out-Host
    return $LASTEXITCODE
}

switch ($Action) {

'status' {
    Write-Host "Dossier du service : $AppDir"
    Write-Host "Configuration      : $configFile"
    if (Test-Path -LiteralPath $configFile) {
        $ts = (Get-Item -LiteralPath $configFile).LastWriteTime
        Write-Host "                     presente ($ts)"
    } else {
        Write-Host "                     ABSENTE"
        exit 1
    }
    Write-Host "Exemple du depot   : $example"
    Write-Host ("Binaire            : {0}{1}" -f $binary,
                $(if (Test-Path -LiteralPath $binary) { "" } else { " (absent)" }))
    Write-Host ""
    exit (Invoke-Check)
}

'check' {
    Assert-Config
    exit (Invoke-Check)
}

'diff' {
    Assert-Config
    if (-not (Test-Path -LiteralPath $example)) { throw "Exemple introuvable : $example" }
    # Les cles _comment documentent l'exemple : les afficher noierait les vraies
    # differences sous du commentaire.
    $py = Get-Python
    $script = @'
import json, sys, difflib
def strip(o):
    if isinstance(o, dict):
        return {k: strip(v) for k, v in o.items() if not k.startswith("_comment")}
    if isinstance(o, list):
        return [strip(v) for v in o]
    return o
ex, live = (strip(json.load(open(p, encoding="utf-8-sig"))) for p in sys.argv[1:3])
a = json.dumps(ex, indent=2, ensure_ascii=False, sort_keys=True).splitlines()
b = json.dumps(live, indent=2, ensure_ascii=False, sort_keys=True).splitlines()
out = list(difflib.unified_diff(a, b, fromfile="exemple du depot",
                                tofile="configuration deployee", lineterm=""))
print("\n".join(out) if out else "Aucune difference (hors commentaires).")
'@
    $tmp = [IO.Path]::GetTempFileName() + ".py"
    Set-Content -LiteralPath $tmp -Value $script -Encoding UTF8
    try { & $py $tmp $example $configFile } finally { Remove-Item -LiteralPath $tmp -Force }
}

'merge' {
    Assert-Config
    if (-not (Test-Path -LiteralPath $example)) { throw "Exemple introuvable : $example" }
    $py = Get-Python
    $backup = New-Backup
    $added = & $py (Join-Path $linuxDir "merge-config.py") $example $configFile
    if ($added) {
        Write-Host "Cles ajoutees (valeurs existantes inchangees) :"
        $added | ForEach-Object { Write-Host "    $_" }
        Write-Host "Sauvegarde : $backup"
        Write-Host ""
        Write-Host "Redemarrer pour appliquer :  Start-ScheduledTask -TaskName $TaskName"
    } else {
        Remove-Item -LiteralPath $backup -Force
        Write-Host "Rien a ajouter : la configuration a deja toutes les cles de l'exemple."
        Write-Host ""
        Write-Host "Note : 'merge' n'ajoute que ce qui MANQUE. Si le service ne collecte"
        Write-Host "rien alors que la fusion ne trouve rien a ajouter, la cause est une"
        Write-Host "valeur existante devenue invalide -- lancer 'check'."
    }
}

'reset' {
    Assert-Config
    if (-not (Test-Path -LiteralPath $example)) { throw "Exemple introuvable : $example" }
    Write-Host "Cette action REMPLACE la configuration deployee par l'exemple du depot."
    Write-Host "  deployee : $configFile"
    Write-Host "  exemple  : $example"
    Write-Host ""
    Write-Host "Tout reglage local (adresses, ports, seuils) sera perdu et devra etre"
    Write-Host "ressaisi. Une sauvegarde datee est conservee a cote du fichier."
    Write-Host ""
    Write-Host "Preferer 'merge' si seules des cles manquent, et 'diff' pour voir ce"
    Write-Host "qui change avant de decider."
    Write-Host ""

    # -Yes n'existe que pour l'automatisation (banc de test, reprovisionnement).
    if ($Yes) {
        Write-Host "Confirmation fournie par -Yes."
    } else {
        $answer = Read-Host "Taper REMPLACER pour confirmer"
        if ($answer -cne "REMPLACER") { Write-Host "Annule."; exit 1 }
    }

    $backup = New-Backup
    Copy-Item -LiteralPath $example -Destination $configFile -Force
    Write-Host "Configuration remplacee. Sauvegarde : $backup"
    Write-Host ""
    Invoke-Check | Out-Null
    Write-Host ""
    Write-Host "Redemarrer pour appliquer :  Start-ScheduledTask -TaskName $TaskName"
}

}
