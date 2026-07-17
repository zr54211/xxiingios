# iOS-слой

Состояние: **этап 0 написан** — `IosScanner.mm` показывает полноэкранный оверлей
поверх окна 1С (key window: сцены iOS 13+ и классический путь), кнопка «Закрыть»
шлёт `ScanCancelled` в BSL. Камеры ещё нет — это прототип риска №1
(свой UI из ВК на iOS, прецедентов не найдено). Проверка — первой сборкой IPA.

Дальше (см. `REQUIREMENTS.md`):

1. **Этап 1** — камера: `AVCaptureSession` 1080p, Y-плоскость `CVPixelBuffer` →
   `bsz::DecodeLuminanceEx`, `ScanResult` в BSL.
2. **Этап 2** — UX-паритет с Android: уголки-сопровождение, фонарик,
   tap-to-focus, autoClose.

Если этап 0 на устройстве провалится — режим Б: `РаспознатьИзображение` по фото
от `СредстваМультимедиа.СделатьФотоснимок`, без своего UI.

## Сборка (без Mac)

GitHub Actions `.github/workflows/ios-lib.yml` (репо `zr54211/xxiingios`,
раннер macos-15/Xcode 16 — libc++ Xcode 15 не собирает C++20-код zxing-cpp):
CMake Ninja `-DCMAKE_SYSTEM_NAME=iOS`, arm64, target 11.0 → `libtool -static`
склеивает с zxing-cpp → артефакт `libBarcodeScannerZXing-ios-arm64`.
Скачанный `.a` кладётся в `build-ios/` — его ждёт `package/make-zip.ps1`.

Факты механизма ВК iOS (разведка по prjios.zip 8.3.27.70):

- линковка в приложение статическая, у проекта платформы `-all_load` —
  библиотека въезжает целиком;
- загрузчик (`addncpp`) ищет **стандартные** имена `GetClassNames` /
  `GetClassObject` / `DestroyObject` / `SetPlatformCapabilities` — без
  суффиксов, наш `exports.cpp` общий для всех ОС;
- следствие: две Native API ВК в одном приложении не уживутся
  (дубль символов) — для кассы не актуально.

## Логи с устройства

`NSLog` с префиксом `BarcodeScannerZXing`; на Windows смотреть через
`idevicesyslog` (libimobiledevice) — аналог `adb logcat`.
