# iOS-слой

Состояние: **готово, проверено на устройстве** (iPhone XS, iOS 18, платформа 8.3.27).
Полный UX-паритет с Android: камера 1080p, уголки-сопровождение со скруглёнными
изломами, фонарик, tap-to-focus, автозакрытие с паузой 0.5 с после распознавания.

## Механизм подключения статической ВК (главное отличие от Android)

- ВК статически линкуется в исполняемый файл приложения; проект платформы
  собирается с `-all_load`, библиотека въезжает целиком.
- Загрузчик ВК **не использует dlsym**: компонента обязана сама
  зарегистрироваться при загрузке процесса вызовом
  `RegisterLibrary(имя, 0, таблица)` из статического конструктора
  (см. `core/exports.cpp`, блок `__APPLE__`; прототип — шаблон templateMobile
  из комплекта «Технология создания внешних компонент»).
- Точки входа дополнительно помечены `visibility("default"), weak` — только
  weak-определения ld64 кладёт в export trie исполняемого файла.
- Следствие: две Native API ВК в одном приложении не уживутся (единый реестр
  стандартных имён точек входа).

## Архитектура (`IosScanner.mm`)

- `BSZScanner` — сессия `AVCaptureSession` (1920x1080, 420f FullRange),
  кадры в ориентации сенсора без CPU-поворота; Y-плоскость →
  `bsz::DecodeLuminanceEx`; маппинг точек кода в координаты экрана через
  `AVCaptureVideoPreviewLayer pointForCaptureDevicePointOfInterest`.
- Оверлей живёт в **собственном UIWindow** (`UIWindowLevelAlert + 1`) — формы
  1С, открываемые сразу после доставки штрихкода, его не перекрывают
  (паритет с отдельной Activity Android).
- `BSZMarkerView` — CADisplayLink-анимация уголков (lerp 0.35), скругление
  изломов дугой 9 pt (паритет с `CornerPathEffect` Android).
- Автозакрытие: 0.5 с после `ScanResult`; внешний `StopScanning` в это окно
  **не обрывает** паузу (касса закрывает сканер сразу после обработки
  штрихкода — экран дозакрывается сам, диалоги показываются после).
- Ошибки пишутся `NSLog` с префиксом `BarcodeScannerZXing`; смотреть с Windows —
  `pymobiledevice3 syslog live` или `idevicesyslog`.

## Сборка (без Mac)

GitHub Actions `.github/workflows/ios-lib.yml` (репо `zr54211/xxiingios`,
раннер macos-15/Xcode 16 — libc++ Xcode 15 не собирает C++20-код zxing-cpp):
CMake Ninja `-DCMAKE_SYSTEM_NAME=iOS`, arm64, target 11.0 → `libtool -static`
склеивает с zxing-cpp → артефакт `libBarcodeScannerZXing-ios-arm64`.
Готовый `.a` кладётся в `iOS/` внутри zip-макета (`arch="universal"`
в manifest.xml — сборщик 1С кладёт iOS-ВК только в `iOS\Universal\`).
