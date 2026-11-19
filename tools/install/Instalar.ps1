<#
 Instalador de Litematica Port Bedrock (cliente + Companion opcional).
 Uso (con Minecraft CERRADO):
   .\Instalar.ps1                          # busca instancias de LeviLauncher y pregunta cual
   .\Instalar.ps1 -Instancia "C:\ruta\a\la\instancia"
   .\Instalar.ps1 -SinCompanion            # no copia el behavior pack
 Nunca cierra el juego ni toca otros mods. Hace copia de seguridad de lo que reemplaza.
#>
param(
    [string]$Instancia,
    [switch]$SinCompanion
)
$ErrorActionPreference = 'Stop'
$raiz = $PSScriptRoot
$mod = Join-Path $raiz 'LHolo'
$bp  = Join-Path $raiz 'Litematica_Realm_Companion_BP'
if (!(Test-Path (Join-Path $mod 'LHolo.dll'))) { throw "No encuentro LHolo\LHolo.dll junto a este script. Ejecutalo desde la carpeta del release descomprimido." }

if (Get-Process -Name 'Minecraft.Windows' -ErrorAction SilentlyContinue) {
    throw 'Cierra Minecraft primero. Este script no lo cierra por ti.'
}

if (!$Instancia) {
    $base = Join-Path $env:LOCALAPPDATA 'LeviLauncher\Instances'
    $lista = @()
    if (Test-Path $base) { $lista = @(Get-ChildItem $base -Directory) }
    if ($lista.Count -eq 0) { throw "No encontre instancias de LeviLauncher en $base. Pasa la ruta con -Instancia." }
    if ($lista.Count -eq 1) { $Instancia = $lista[0].FullName }
    else {
        for ($i = 0; $i -lt $lista.Count; $i++) { Write-Host ("[{0}] {1}" -f $i, $lista[$i].Name) }
        $n = Read-Host 'Numero de la instancia'
        $Instancia = $lista[[int]$n].FullName
    }
}
$Instancia = (Resolve-Path -LiteralPath $Instancia).Path
Write-Host "Instancia: $Instancia"

$mods = Join-Path $Instancia 'mods'
if (!(Test-Path (Join-Path $mods 'LeviLamina')) -and !(Test-Path $mods)) {
    Write-Warning 'No veo la carpeta mods. Comprueba que LeviLamina (cliente) 26.60.* esta instalado en esa instancia.'
}
New-Item -ItemType Directory -Force -Path $mods | Out-Null

$sello = Get-Date -Format 'yyyyMMdd-HHmmss'
$destino = Join-Path $mods 'LHolo'
if (Test-Path $destino) {
    $copia = Join-Path $mods "LHolo.respaldo-$sello"
    Write-Host "Copia de seguridad: $copia"
    Copy-Item -LiteralPath $destino -Destination $copia -Recurse
}
# config/ y data/ son del usuario: no se pisan.
New-Item -ItemType Directory -Force -Path $destino | Out-Null
Get-ChildItem -LiteralPath $mod -Force | Where-Object { $_.Name -notin @('config', 'data') } |
    ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $destino -Recurse -Force }
Write-Host 'Mod copiado.'

if (!$SinCompanion -and (Test-Path $bp)) {
    $bpRoot = Join-Path $Instancia 'Minecraft Bedrock\Users\Shared\games\com.mojang\behavior_packs'
    if (!(Test-Path $bpRoot)) { New-Item -ItemType Directory -Force -Path $bpRoot | Out-Null }
    $bpDest = Join-Path $bpRoot 'Litematica_Realm_Companion_BP'
    if (Test-Path $bpDest) { Move-Item -LiteralPath $bpDest -Destination "$bpDest.respaldo-$sello" }
    Copy-Item -LiteralPath $bp -Destination $bpDest -Recurse
    Write-Host 'Companion copiado (hay que activarlo en el mundo/Realm, ver README).'
}
Write-Host 'Listo. Abre Minecraft tu mismo.'
