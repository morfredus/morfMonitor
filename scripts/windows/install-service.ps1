<#
    install-service.ps1 — Installe morfMonitor en service Windows.

    Windows n'a pas d'equivalent direct de systemd pour un simple exe console.
    L'approche la plus robuste SANS dependance externe est une tache planifiee
    "au demarrage" (Planificateur de taches), executee par le compte SYSTEM,
    redemarree automatiquement en cas d'arret. Le binaire et sa configuration
    sont copies dans un dossier fixe (par defaut C:\ProgramData\morfmonitor),
    independant du clone git.

    A lancer dans une PowerShell ADMINISTRATEUR :
        powershell -ExecutionPolicy Bypass -File scripts\windows\install-service.ps1
        ... -AppDir "D:\services\morfmonitor"     # autre dossier
        ... -Uninstall                             # desinstaller

    Prerequis : avoir compile le binaire au prealable
        cmake --preset mingw ; cmake --build --preset mingw
    (ou fournir -BinPath vers un morfmonitor.exe deja compile).
#>
param(
    [string]$AppDir  = "$env:ProgramData\morfmonitor",
    [string]$BinPath = "",
    [switch]$Uninstall
)
$ErrorActionPreference = "Stop"
$TaskName = "morfmonitor"

function Assert-Admin {
    $id = [Security.Principal.WindowsIdentity]::GetCurrent()
    $p  = New-Object Security.Principal.WindowsPrincipal($id)
    if (-not $p.IsInRole([Security.Principal.WindowsBuiltinRole]::Administrator)) {
        throw "Ce script doit etre lance dans une PowerShell Administrateur."
    }
}
Assert-Admin

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)   # racine du projet

# --- Desinstallation ------------------------------------------------------
if ($Uninstall) {
    Write-Host "Desinstallation de la tache '$TaskName'..."
    schtasks /End    /TN $TaskName 2>$null | Out-Null
    schtasks /Delete /TN $TaskName /F 2>$null | Out-Null
    Write-Host "Tache supprimee. (Dossier $AppDir conserve — le retirer a la main si besoin.)"
    exit 0
}

# --- 1. Trouver le binaire ------------------------------------------------
if (-not $BinPath) {
    $candidates = @(
        "$repoRoot\build-mingw\service\morfmonitor.exe",
        "$repoRoot\build\service\morfmonitor.exe"
    )
    $BinPath = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
}
if (-not $BinPath -or -not (Test-Path $BinPath)) {
    throw "Binaire introuvable. Compiler d'abord (cmake --build --preset mingw) ou passer -BinPath."
}

# --- 2. Copier binaire + config dans le dossier fixe ----------------------
Write-Host "Installation dans : $AppDir"
New-Item -ItemType Directory -Force -Path $AppDir | Out-Null
Copy-Item $BinPath "$AppDir\morfmonitor.exe" -Force

$cfgDst = "$AppDir\morfmonitor.json"
if (-not (Test-Path $cfgDst)) {
    Copy-Item "$repoRoot\config\morfmonitor.example.json" $cfgDst
    Write-Host "Config initiale copiee : $cfgDst (a adapter)."
} else {
    Write-Host "Config existante conservee : $cfgDst"
}

# --- 3. Enregistrer la tache planifiee (au demarrage, compte SYSTEM) ------
schtasks /End    /TN $TaskName 2>$null | Out-Null
schtasks /Delete /TN $TaskName /F 2>$null | Out-Null

$action    = New-ScheduledTaskAction  -Execute "$AppDir\morfmonitor.exe" `
                                       -Argument "--config `"$cfgDst`"" -WorkingDirectory $AppDir
$trigger   = New-ScheduledTaskTrigger -AtStartup
$principal = New-ScheduledTaskPrincipal -UserId "SYSTEM" -LogonType ServiceAccount -RunLevel Highest
# Redemarre la tache si elle s'arrete ; pas de limite d'execution.
$settings  = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries `
                 -RestartInterval (New-TimeSpan -Minutes 1) -RestartCount 9999 `
                 -ExecutionTimeLimit (New-TimeSpan -Seconds 0)

Register-ScheduledTask -TaskName $TaskName -Action $action -Trigger $trigger `
                       -Principal $principal -Settings $settings -Force | Out-Null

Start-ScheduledTask -TaskName $TaskName
Start-Sleep -Seconds 1
Write-Host "Service '$TaskName' installe (tache planifiee, demarrage auto) et lance."
Write-Host "Etat  :  schtasks /Query /TN $TaskName"
Write-Host "Arret :  schtasks /End   /TN $TaskName"
Write-Host "Test  :  curl http://127.0.0.1:8790/status"
