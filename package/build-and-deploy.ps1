# Полный цикл: ВК -> тестовая конфигурация -> APK в сборщике -> установка на устройство.
# Использование: pwsh package/build-and-deploy.ps1 [-SkipNative] [-NoInstall]
#   -SkipNative  не пересобирать C++ (менялась только Java-часть)
#   -NoInstall   не ставить APK на устройство
param(
    [switch]$SkipNative,
    [switch]$NoInstall,
    [string]$Device = "R5CY51GXFKW"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

$ninja = "C:\Users\user\AppData\Local\Android\Sdk\cmake\3.22.1\bin\ninja.exe"
$strip = "C:\Users\user\AppData\Local\Android\Sdk\ndk\27.3.13750724\toolchains\llvm\prebuilt\windows-x86_64\bin\llvm-strip.exe"
$v8designer = "C:\Program Files\1cv8\8.3.27.2214\bin\1cv8.exe"
$v8builder = "C:\Program Files\1cv8\8.5.1.1423\bin\1cv8c.exe"
$builderBase = "C:\Bases\Сборщик"
$ib = "$root\test-app\ib\db-data"
$cf = "$root\test-app\cf"
$publish = "$root\test-app\publish"
$apk = "$publish\pb.mobilesn.com.onecvn.scannertest-arm64.apk"

function Step($name, $script) {
    Write-Host "=== $name ===" -ForegroundColor Cyan
    & $script
    if ($LASTEXITCODE -ne 0 -and $null -ne $LASTEXITCODE) { throw "Шаг '$name' завершился с ошибкой ($LASTEXITCODE)" }
}

if (-not $SkipNative) {
    Step "C++ (ninja, arm64)" { & $ninja -C "$root\build-android-arm64" }
    Step "strip" { & $strip -o "$root\build-android-arm64\libBarcodeScannerZXing.stripped.so" "$root\build-android-arm64\libBarcodeScannerZXing.so" }
}

Step "Java-часть ВК" { & pwsh -NoProfile -File "$root\package\make-java.ps1" }
Step "ZIP-макет ВК" { & pwsh -NoProfile -File "$root\package\make-zip.ps1" }

Copy-Item "$root\dist\BarcodeScannerZXing.zip" "$cf\CommonTemplates\BarcodeScannerZXing\Ext\Template.bin" -Force
Write-Host "Template.bin обновлён"

# Загрузка конфигурации и выгрузка мобильного пакета — обязательно два отдельных вызова:
# в комбинированном запуске /MAWriteFile срабатывает до /LoadConfigFromFiles.
Step "Загрузка конфигурации в тестовую ИБ" {
    $p = Start-Process -FilePath $v8designer -ArgumentList 'DESIGNER','/F',$ib,'/DisableStartupDialogs','/DisableStartupMessages',
        '/LoadConfigFromFiles',$cf,'/UpdateDBCfg','/Out',"$publish\designer.log" -Wait -PassThru
    Get-Content "$publish\designer.log"
    if ($p.ExitCode -ne 0) { throw "Конфигуратор: exit $($p.ExitCode)" }
}
Step "Выгрузка 1cema.zip" {
    $p = Start-Process -FilePath $v8designer -ArgumentList 'DESIGNER','/F',$ib,'/DisableStartupDialogs','/DisableStartupMessages',
        '/MAWriteFile',"$publish\1cema.zip",'/Out',"$publish\designer.log" -Wait -PassThru
    Get-Content "$publish\designer.log"
    if ($p.ExitCode -ne 0) { throw "Конфигуратор: exit $($p.ExitCode)" }
}

# Сборка APK расширением AutoBuild в базе сборщика.
# NoDefaultCurrentDirectoryInExePath ломает поиск make.bat/gradlew в текущем каталоге
# (наследуется сборщиком и всеми его дочерними процессами) — убираем перед запуском.
Step "Автосборка APK (сборщик)" {
    Remove-Item "$publish\1cema.zip.autobuild.ok", "$publish\1cema.zip.autobuild.fail" -Force -ErrorAction SilentlyContinue
    Remove-Item Env:\NoDefaultCurrentDirectoryInExePath -ErrorAction SilentlyContinue
    $p = Start-Process -FilePath $v8builder -ArgumentList 'ENTERPRISE','/F',$builderBase,'/N','Администратор',
        '/DisableStartupDialogs','/DisableStartupMessages',
        "/C""autobuild;$publish\1cema.zip;Тест сканера ВК""" -Wait -PassThru
    Get-Content "$publish\1cema.zip.autobuild.log" -Encoding UTF8 | Select-Object -Last 8
    if (-not (Test-Path "$publish\1cema.zip.autobuild.ok")) { throw "Автосборка не удалась, см. $publish\1cema.zip.autobuild.log" }
}

if (-not $NoInstall) {
    Step "Установка на устройство $Device" {
        & adb -s $Device install -r $apk
        & adb -s $Device shell pm grant pb.mobilesn.com.onecvn.scannertest android.permission.CAMERA
        & adb -s $Device shell monkey -p pb.mobilesn.com.onecvn.scannertest 1 | Out-Null
    }
}

Write-Host "=== Готово ===" -ForegroundColor Green
