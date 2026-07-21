<#
.SYNOPSIS
    Met a jour morfMonitor installe en tache planifiee (Windows).

.DESCRIPTION
    Equivalent Windows de scripts/linux/update-service.sh.

    Recupere le code (git pull), recompile, recopie le binaire dans le dossier
    fixe, complete la configuration, verifie qu'elle reste exploitable, puis
    relance la tache.

    Les valeurs deja presentes dans morfmonitor.json ne sont JAMAIS modifiees ;
    seuls les parametres apparus depuis l'installation y sont ajoutes, puis
    listes. Pour ECRASER la configuration par celle du depot, utiliser
    deploy-config.ps1.

.EXAMPLE
    .\scripts\windows\update-service.ps1
    .\scripts\windows\update-service.ps1 -NoPull
    .\scripts\windows\update-service.ps1 -NoConfig
#>
param(
    [string]$AppDir = "$env:ProgramData\morfmonitor",
    [string]$Preset = "mingw",
    [switch]$NoPull,
    [switch]$NoConfig
)

$ErrorActionPreference = "Stop"
$TaskName = "morfmonitor"
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$linuxDir = Join-Path $repoRoot "scripts\linux"   # les .py y vivent

function Assert-Admin {
    $id = [Security.Principal.WindowsIdentity]::GetCurrent()
    $p  = New-Object Security.Principal.WindowsPrincipal($id)
    if (-not $p.IsInRole([Security.Principal.WindowsBuiltinRole]::Administrator)) {
        throw "Ce script doit etre lance dans une PowerShell Administrateur."
    }
}
Assert-Admin

if (-not (Test-Path -LiteralPath $AppDir)) {
    throw "Service non installe : $AppDir absent.`nLancer d'abord : .\scripts\windows\install-service.ps1"
}

# cmake et git sont des executables natifs : leur code de retour doit etre
# verifie explicitement, sinon un echec de configuration passe inapercu et la
# compilation suivante travaille sur une arborescence perimee.
function Invoke-Native {
    $exe  = $args[0]
    $rest = if ($args.Count -gt 1) { $args[1..($args.Count - 1)] } else { @() }
    & $exe @rest
    if ($LASTEXITCODE -ne 0) { throw "$exe $rest a echoue (code $LASTEXITCODE)" }
}

Push-Location $repoRoot
try {
    if (-not $NoPull) {
        Write-Host "git pull..."
        Invoke-Native git pull --ff-only
    }

    Write-Host "Compilation (preset $Preset)..."
    Invoke-Native cmake --preset $Preset
    Invoke-Native cmake --build --preset $Preset

    $bin = Join-Path $repoRoot "build-$Preset\service\morfmonitor.exe"
    if (-not (Test-Path -LiteralPath $bin)) {
        throw "Echec : $bin introuvable apres compilation."
    }

    # --- Recopier le binaire (config preservee) --------------------------
    Write-Host "Copie du binaire vers $AppDir..."
    schtasks /End /TN $TaskName 2>$null | Out-Null
    Start-Sleep -Milliseconds 500
    Copy-Item -LiteralPath $bin -Destination (Join-Path $AppDir "morfmonitor.exe") -Force

    # --- Completer la configuration --------------------------------------
    $configFile = Join-Path $AppDir  "morfmonitor.json"
    $example    = Join-Path $repoRoot "config\morfmonitor.example.json"

    if (-not $NoConfig -and (Test-Path -LiteralPath $example)) {
        if (-not (Test-Path -LiteralPath $configFile)) {
            Copy-Item -LiteralPath $example -Destination $configFile
            Write-Host "Config absente : copiee depuis l'exemple -> $configFile (a adapter)."
        } else {
            $py = $null
            foreach ($c in @('python', 'python3', 'py')) {
                $cmd = Get-Command $c -ErrorAction SilentlyContinue
                if ($cmd) { $py = $cmd.Source; break }
            }
            if (-not $py) {
                Write-Warning "Python absent : configuration non completee (voir $example)."
            } else {
                $backup = "$configFile.bak-$(Get-Date -Format 'yyyyMMdd-HHmmss')"
                Copy-Item -LiteralPath $configFile -Destination $backup

                $added = & $py (Join-Path $linuxDir "merge-config.py") $example $configFile
                if ($added) {
                    Write-Host ""
                    Write-Host "Nouveaux parametres ajoutes a $configFile :"
                    $added | ForEach-Object { Write-Host "    $_" }
                    Write-Host "  (valeurs existantes inchangees ; sauvegarde : $backup)"
                    Write-Host "  A RENSEIGNER si besoin avant que la fonction correspondante s'active."
                    Write-Host ""
                } else {
                    Remove-Item -LiteralPath $backup -Force
                }

                # La fusion ajoute ce qui MANQUE ; elle ne corrige pas une valeur
                # deja presente devenue invalide. Cette verification ne modifie
                # rien : elle constate, pour que la mise a jour ne laisse pas
                # derriere elle un service muet.
                $check = Join-Path $linuxDir "check-config.py"
                if (Test-Path -LiteralPath $check) {
                    Write-Host ""
                    & $py $check $configFile --binary (Join-Path $AppDir "morfmonitor.exe") `
                          --example $example --hint-style ps1
                    if ($LASTEXITCODE -ne 0) {
                        Write-Host ""
                        Write-Host "La configuration deployee ne permettra pas au service de collecter."
                        Write-Host "Inspecter puis corriger :"
                        Write-Host "    .\scripts\windows\config-tool.ps1 diff"
                        Write-Host "    .\scripts\windows\deploy-config.ps1"
                        Write-Host "La mise a jour se poursuit : ce diagnostic ne modifie rien."
                    }
                    Write-Host ""
                }
            }
        }
    }

    # --- Relancer ---------------------------------------------------------
    Start-ScheduledTask -TaskName $TaskName
    Start-Sleep -Seconds 1
    Write-Host "Mise a jour appliquee."
    Write-Host "Etat     :  schtasks /Query /TN $TaskName"
    Write-Host "Verifier :  curl http://127.0.0.1:8790/api/all"
}
finally {
    Pop-Location
}
