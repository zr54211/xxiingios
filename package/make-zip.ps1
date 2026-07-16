# Сборка zip-макета внешней компоненты из результатов сборки.
# Использование: pwsh package/make-zip.ps1 [-OutDir dist]
#
# Ожидаемая раскладка бинарников (создаётся сборками CMake, см. README):
#   build-android-arm64/libBarcodeScannerZXing.so  -> Android/arm64-v8a/
#   build-android-arm32/libBarcodeScannerZXing.so  -> Android/armeabi-v7a/
#   build-android-x64/libBarcodeScannerZXing.so    -> Android/x86_64/
#   build-ios/libBarcodeScannerZXing.a             -> iOS/
param(
    [string]$OutDir = "dist"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

$staging = Join-Path $root "$OutDir/staging"
if (Test-Path $staging) { Remove-Item -Recurse -Force $staging }
New-Item -ItemType Directory -Force "$staging/Android/arm64-v8a" | Out-Null
New-Item -ItemType Directory -Force "$staging/Android/armeabi-v7a" | Out-Null
New-Item -ItemType Directory -Force "$staging/Android/x86_64" | Out-Null
New-Item -ItemType Directory -Force "$staging/iOS" | Out-Null

$items = @(
    @{ src = "build-java/BarcodeScannerZXing.apk";            dst = "Android" },
    @{ src = "build-android-arm64/libBarcodeScannerZXing.so"; dst = "Android/arm64-v8a" },
    @{ src = "build-android-arm32/libBarcodeScannerZXing.so"; dst = "Android/armeabi-v7a" },
    @{ src = "build-android-x64/libBarcodeScannerZXing.so";   dst = "Android/x86_64" },
    @{ src = "build-ios/libBarcodeScannerZXing.a";            dst = "iOS" }
)

$missing = @()
foreach ($item in $items) {
    $srcPath = Join-Path $root $item.src
    # Предпочитаем stripped-вариант (llvm-strip), если он подготовлен рядом.
    $stripped = $srcPath -replace '\.(so|a)$', '.stripped.$1'
    if (Test-Path $stripped) {
        Copy-Item $stripped (Join-Path (Join-Path $staging $item.dst) (Split-Path $item.src -Leaf))
    } elseif (Test-Path $srcPath) {
        Copy-Item $srcPath (Join-Path $staging $item.dst)
    } else {
        $missing += $item.src
    }
}

if ($missing.Count -gt 0) {
    Write-Warning "Не найдены (будут пропущены): $($missing -join ', ')"
}

Copy-Item (Join-Path $PSScriptRoot "manifest.xml") $staging
Copy-Item (Join-Path $PSScriptRoot "ANDROID_MANIFEST_EXTENTIONS.XML") $staging
Copy-Item (Join-Path $root "deps/zxing-cpp/LICENSE") (Join-Path $staging "LICENSE-zxing-cpp.txt")

$zipPath = Join-Path $root "$OutDir/BarcodeScannerZXing.zip"
if (Test-Path $zipPath) { Remove-Item -Force $zipPath }
Compress-Archive -Path "$staging/*" -DestinationPath $zipPath

Write-Host "Готово: $zipPath"
