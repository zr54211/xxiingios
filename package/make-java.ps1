# Сборка Java-части компоненты (UiBridge) в apk (zip c classes.dex).
# Результат: build-java/BarcodeScannerZXing.apk (подхватывается package/make-zip.ps1).
param(
    [string]$Javac = "C:\Program Files\1C\1CE\components\axiom-jdk-full-17.0.16+12-x86_64\bin\javac.exe",
    [string]$Sdk = "$env:LOCALAPPDATA\Android\Sdk",
    [string]$BuildTools = "35.0.0",
    [string]$Platform = "android-35"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

$androidJar = Join-Path $Sdk "platforms\$Platform\android.jar"
# d8.bat портит аргументы-пути (cmd-парсинг) — зовём d8.jar напрямую.
$d8Jar = Join-Path $Sdk "build-tools\$BuildTools\lib\d8.jar"
$java = Join-Path (Split-Path -Parent $Javac) "java.exe"

$out = Join-Path $root "build-java"
if (Test-Path $out) { Remove-Item -Recurse -Force $out }
New-Item -ItemType Directory -Force "$out\classes", "$out\dex" | Out-Null

$sources = @(Get-ChildItem (Join-Path $root "android\java") -Recurse -Filter *.java |
    ForEach-Object FullName)
& $Javac -encoding UTF-8 -source 8 -target 8 -bootclasspath $androidJar `
    -d "$out\classes" @sources
if ($LASTEXITCODE -ne 0) { throw "javac failed" }

# d8 не принимает абсолютные Windows-пути входных классов — передаём относительные.
Push-Location $root
try {
    $classes = @(Get-ChildItem "build-java\classes" -Recurse -Filter *.class |
        ForEach-Object { $_.FullName.Substring($root.Length + 1) })
    & $java -cp $d8Jar com.android.tools.r8.D8 --release --min-api 21 --lib $androidJar `
        --output "build-java\dex" @classes
    if ($LASTEXITCODE -ne 0) { throw "d8 failed" }
} finally {
    Pop-Location
}

$apk = Join-Path $out "BarcodeScannerZXing.apk"
$zip = "$apk.zip"
if (Test-Path $zip) { Remove-Item -Force $zip }
Compress-Archive -Path "$out\dex\classes.dex" -DestinationPath $zip
Move-Item -Force $zip $apk

Write-Host "Готово: $apk"
