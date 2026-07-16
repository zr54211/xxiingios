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

## Инфраструктура — принятая схема (без своего Mac)

Решение 2026-07-17: локального Mac нет и не требуется. Под mac-тулчейн компилируется
только наша `.a`; сборку приложения берёт облако 1С.

```
GitHub Actions (macOS-раннер, без сертификатов)
    └─> libBarcodeScannerZXing.a        <- только при изменении C++/UI кода ВК
            └─> zip макета ВК (Windows, make-zip.ps1)
                    └─> конфигурация -> 1cema
                            └─> Облачный сервис сборки 1С -> IPA / установка на устройство
```

1. **Облачный «Сервис сборки и публикации мобильных приложений» 1С** — основной
   путь сборки IPA: бета, бесплатный, регистрация на developer.1c.ru, запускается
   стандартной обработкой из платформы (8.3.20+), iOS без macOS, умеет публикацию
   в сторы и установку тестовых сборок на устройство напрямую.
   **Главный вопрос пробы: пропускает ли облачная сборка конфигурации с ВК** —
   проверяется Android-сборкой test-app через облако ещё до написания iOS-кода.
2. **GitHub Actions** — компиляция `.a` (macOS-раннер, Xcode 15+; на Free-тарифе
   ~200 мак-минут/мес — достаточно, операция редкая). Репозиторий — приватный.
3. **Apple Developer** — доступ есть; сертификаты понадобятся на этапе публикации
   (для тестовой доставки через сервис 1С — уточнить при пробе).
4. **Тестовое устройство** iPhone (iOS 15+); логи с устройства на Windows —
   `idevicesyslog` (libimobiledevice), аналог adb logcat.
5. **Запасной путь** (если облако не пропустит ВК): полная CI-сборка IPA в
   GitHub Actions (распаковка prjios_client.zip, xcodebuild, подпись из secrets,
   TestFlight через fastlane) — либо физический Mac mini M1/M2 + штатный сборщик
   по SSH (`BuildIPA`, константы MacComputerAddress + PuTTY).

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

1. Регистрация на developer.1c.ru (учётка пользователя) — нужна для первого шага.
2. Пропускает ли облачный сервис 1С конфигурации с ВК (проба Android test-app).
3. Целевые iPhone/iPad и минимальная версия iOS в проде?
4. Формат распространения кассы на iOS: доставка сервисом 1С / TestFlight / MDM?
