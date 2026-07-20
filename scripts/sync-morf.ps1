# Resynchronise la copie vendorée de morfBeacon dans third_party/morf/beacon
# depuis le dépôt source voisin.
#
# Source par défaut : le dossier parent du projet (ex. 01-Travail/).
# Surcharge possible : $env:MORF_SRC_BASE = "C:\chemin\vers\depots"
$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot        # racine du projet
$srcBase = if ($env:MORF_SRC_BASE) { $env:MORF_SRC_BASE } else { Split-Path -Parent $root }

function Sync-One($name, $srcDir, $dstDir) {
    if (-not (Test-Path $srcDir)) {
        Write-Error "Source introuvable pour $name : $srcDir (définir MORF_SRC_BASE si ailleurs)"
    }
    Remove-Item -Recurse -Force "$dstDir\include", "$dstDir\src" -ErrorAction SilentlyContinue
    Copy-Item -Recurse "$srcDir\include" "$dstDir\include"
    Copy-Item -Recurse "$srcDir\src"     "$dstDir\src"
    Copy-Item "$srcDir\VERSION" "$dstDir\VERSION"
    $v = (Get-Content "$dstDir\VERSION" -First 1).Trim()
    Write-Output "OK  $name  (version $v)"
}

# morfMonitor n'embarque que morfBeacon. Le depot source peut s'appeler
# « morfBeacon » ou « morfBeacon » : on prend le premier trouve.
$beaconSrc = if (Test-Path "$srcBase\morfBeacon") { "$srcBase\morfBeacon" } else { "$srcBase\morfBeacon" }

Sync-One "morfBeacon" $beaconSrc "$root\third_party\morf\beacon"
Write-Output "Synchronisation terminee. Le CMakeLists vendore n'est pas modifie."
