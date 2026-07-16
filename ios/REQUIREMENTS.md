# iOS-часть BarcodeScannerZXing — требования

Цель: паритет с Android-частью. Тот же контракт Native API (НачатьСканирование /
ЗавершитьСканирование / РаспознатьКадр / РаспознатьИзображение / ВключитьФонарик,
события `ScanResult` / `ScanCancelled`, тот же JSON), тот же UX сканера
(рамка наводки, жёлтые уголки-сопровождение, фонарик, tap-to-focus, autoClose).

## Факты платформы (проверено по prjios.zip из mobile_8_3_27_70.zip)

| Параметр | Значение | Следствие |
|---|---|---|
| `IPHONEOS_DEPLOYMENT_TARGET` | **11.0** | Наш `CMAKE_OSX_DEPLOYMENT_TARGET=11.0` |
| `VALID_ARCHS` | **arm64** (только) | Одна архитектура; симуляторные prj*_sim — отдельно, опционально |
| `NSCameraUsageDescription` | **уже в Info.plist платформы** («…scan barcodes») | Разрешение камеры из коробки; механизм расширения plist НЕ нужен (в отличие от Android-манифеста) |

Поставка в zip ВК: `<component os="iOS" arch="arm64" path="iOS/libBarcodeScannerZXing.a" type="native"/>`
(строка уже заготовлена в `package/manifest.xml`, `make-zip.ps1` ждёт `build-ios/libBarcodeScannerZXing.a`).
Сборщик ждёт **один** файл `.a` — zxing-cpp и код компоненты объединяются
(`libtool -static -o` либо CMake OBJECT-библиотеки в одну статическую цель).

## Архитектура (проще Android)

- **Язык:** Objective-C++ (`.mm`). Java-мост не нужен: переключение в UI-поток —
  `dispatch_async(dispatch_get_main_queue(), ^{...})` прямо из C++.
- **Точка входа в UI:** `UIApplication.sharedApplication` → key window →
  `rootViewController`. Аналога `IAndroidComponentHelper` нет — это **риск №1**,
  снимается прототипом этапа 0 (пустой полноэкранный `UIView` поверх окна 1С).
- **Камера:** AVFoundation. `AVCaptureSession` (preset 1920x1080),
  выход `AVCaptureVideoDataOutput` с `kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange`,
  Y-плоскость → существующий `bsz::DecodeLuminanceEx` без изменений.
- **Оверлей:** `UIView` поверх `rootViewController.view`; превью —
  `AVCaptureVideoPreviewLayer`; уголки — `CAShapeLayer` (перетекание —
  `CABasicAnimation` или пошагово, как на Android); кнопки ✕ и 🔦.
- **Фонарик:** `AVCaptureDevice.torchMode` (`lockForConfiguration`).
- **Tap-to-focus:** `focusPointOfInterest` + `exposurePointOfInterest`,
  режим `AVCaptureFocusModeAutoFocus`, возврат в `ContinuousAutoFocus` по таймеру
  (та же логика 4 с, что на Android).
- **«Назад»:** аппаратной кнопки нет — закрытие крестиком; опционально свайп вниз.
- **Общий код:** `core/` (BarcodeScannerAddIn, Decoder, StringUtils) кроссплатформенный,
  переиспользуется как есть; платформо-зависимые файлы — `ios/IosScanner.mm`,
  `ios/CameraScanner.mm` (зеркально android/).

## Инфраструктура — что нужно подготовить

1. **Mac** с Xcode (для iOS SDK, совместимого с платформой 8.3.27 — Xcode 15+)
   и CMake (brew). Указать: какой Mac есть в хозяйстве, версия macOS.
2. **Сборщик**: умеет собирать iOS удалённо через Mac по SSH — константы
   `MacComputerAddress/User/Password` + PuTTY в `PathsToComponents`, флаг `BuildIPA`.
   Настроить и проверить на пустой конфигурации до начала работ по ВК.
3. **Apple Developer**: аккаунт (организация 1С Vietnam?), development-сертификат
   и provisioning-профиль для тестового устройства. Для внутренней раздачи —
   решить формат: TestFlight / ad-hoc / MDM.
4. **Тестовое устройство** iPhone (iOS 15+), кабель/Wi-Fi отладка, те же тестовые
   образцы кодов (плотный QR на мятой плёнке, марка GS1 DataMatrix).

## Этапы (зеркало Android-пути)

0. **Прототип риска UI** (главное): из метода ВК показать/скрыть пустой
   полноэкранный UIView поверх окна 1С; событие в BSL доходит.
   Провал ⇒ откат на режим Б («РаспознатьИзображение» по фото без своего UI).
1. **Камера + декод**: AVCapture → Y-plane → zxing → `ScanResult` → BSL.
2. **UX-паритет**: уголки/фонарик/tap-to-focus/autoClose/только 2D-форматы.
3. **Интеграция**: в МобильнойКассе уже готова — пункт «Камера (улучшенное
   распознавание)» показывается на любом мобильном приложении; на iOS до
   готовности `.a` подключение честно вернёт предупреждение.

## Предусловия релиза (Android-хвосты, до iOS)

- Собрать `armeabi-v7a` и `x86_64` варианты `.so` и включить в zip:
  сборщик кладёт в APK каждой архитектуры только те библиотеки, что есть в
  макете — сейчас там только arm64.
- Боевой Application ID в группе «1С:POS» сборщика (сейчас временный от теста).

## Открытые вопросы (ответить до старта)

1. Какой Mac доступен (модель, macOS, Xcode)? Постоянно в сети для сборщика?
2. Apple Developer аккаунт: чей, есть ли сертификаты/профили?
3. Целевые iPhone/iPad и минимальная версия iOS в проде?
4. Формат распространения кассы на iOS (TestFlight / MDM / ad-hoc)?
