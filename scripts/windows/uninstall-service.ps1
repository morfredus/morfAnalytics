<#
    uninstall-service.ps1 — Supprime entierement le service morfAnalytics, pour
    permettre une reinstallation depuis zero.

    Le -Uninstall de install-service.ps1 se contente de retirer la tache
    planifiee et CONSERVE le dossier d'installation : pratique pour une mise a
    jour, mais une reinstallation « propre » repartait alors de l'ancienne
    configuration et de l'ancien cache. Ce script-ci retire tout par defaut.

    Ce qui est supprime :
      - la tache planifiee et son declenchement au demarrage ;
      - le dossier d'installation (binaire, configuration, cache de travail).

    Ce qui n'est JAMAIS touche : le clone git. Le depot reste intact.

    Le cache de travail est une COPIE des mesures de l'appareil, jamais la
    source de verite : le supprimer ne perd rien. Il sera reconstitue a la
    premiere collecte suivant la reinstallation (comptez quelques minutes si
    l'historique est long).

    A lancer dans une PowerShell ADMINISTRATEUR :
        powershell -ExecutionPolicy Bypass -File scripts\windows\uninstall-service.ps1
        ... -KeepConfig                          # sauvegarder le .json
        ... -DryRun                              # montrer sans rien supprimer
        ... -AppDir "D:\services\morfanalytics"  # autre dossier
#>
param(
    [string]$AppDir = "$env:ProgramData\morfanalytics",
    [switch]$KeepConfig,
    [switch]$DryRun
)
$ErrorActionPreference = "Stop"
$TaskName = "morfanalytics"
$ConfigFile = Join-Path $AppDir "morfanalytics.json"

function Assert-Admin {
    $id = [Security.Principal.WindowsIdentity]::GetCurrent()
    $p  = New-Object Security.Principal.WindowsPrincipal($id)
    if (-not $p.IsInRole([Security.Principal.WindowsBuiltinRole]::Administrator)) {
        throw "Ce script doit etre lance dans une PowerShell Administrateur."
    }
}
Assert-Admin

Write-Host "Tache        : $TaskName"
Write-Host "Installation : $AppDir"
if ($KeepConfig) { Write-Host "Configuration : conservee" }
if ($DryRun)     { Write-Host "Mode         : simulation (rien ne sera supprime)" }
Write-Host ""

# --- 1. Arreter et supprimer la tache planifiee ---------------------------
$task = Get-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue
if ($task) {
    Write-Host "Arret et suppression de la tache planifiee..."
    if (-not $DryRun) {
        schtasks /End    /TN $TaskName 2>$null | Out-Null
        schtasks /Delete /TN $TaskName /F 2>$null | Out-Null
    } else {
        Write-Host "  [simulation] schtasks /End /TN $TaskName"
        Write-Host "  [simulation] schtasks /Delete /TN $TaskName /F"
    }
} else {
    Write-Host "Aucune tache planifiee '$TaskName' declaree."
}

# --- 2. Attendre l'arret du processus -------------------------------------
# Le dossier ne peut pas etre supprime tant que l'executable tourne encore :
# Windows verrouille le fichier, et la suppression echouerait sans expliquer
# pourquoi.
$proc = Get-Process -Name "morfanalytics" -ErrorAction SilentlyContinue
if ($proc) {
    Write-Host "Arret du processus morfanalytics..."
    if (-not $DryRun) {
        $proc | Stop-Process -Force
        Start-Sleep -Milliseconds 700
    } else {
        Write-Host "  [simulation] Stop-Process morfanalytics"
    }
}

# --- 3. Retirer le dossier d'installation ---------------------------------
if (Test-Path $AppDir) {
    $backup = $null
    if ($KeepConfig -and (Test-Path $ConfigFile)) {
        $backup = Join-Path $env:TEMP ("morfanalytics.json." + (Get-Date -Format "yyyyMMdd-HHmmss"))
        Write-Host "Sauvegarde de la configuration : $backup"
        if (-not $DryRun) { Copy-Item $ConfigFile $backup }
    }
    Write-Host "Suppression du dossier d'installation."
    if (-not $DryRun) {
        Remove-Item -Recurse -Force $AppDir
    } else {
        Write-Host "  [simulation] Remove-Item -Recurse -Force $AppDir"
    }
    if ($KeepConfig -and $backup) {
        Write-Host ""
        Write-Host "Configuration conservee dans $backup"
        Write-Host "Apres reinstallation, la remettre en place :"
        Write-Host "  Copy-Item `"$backup`" `"$ConfigFile`" -Force"
    }
} else {
    Write-Host "Dossier $AppDir deja absent."
}

Write-Host ""
if ($DryRun) {
    Write-Host "Simulation terminee : rien n'a ete supprime."
} else {
    Write-Host "Desinstallation terminee. Le clone git n'a pas ete touche."
    Write-Host "Reinstaller :  powershell -ExecutionPolicy Bypass -File scripts\windows\install-service.ps1"
}
