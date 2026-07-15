# Android-слой (этап 2)

Здесь появится платформенная часть для Android:

- **ScannerActivity** (Kotlin/Java): полноэкранный сканер — CameraX (`Preview` +
  `ImageAnalysis` 1080p+), continuous autofocus, tap-to-focus, зум, фонарик.
  Кадры (Y-плоскость `ImageProxy`) передаются в `bsz::DecodeLuminance`.
- **JNI-мост**: получение Activity приложения 1С через
  `IAndroidComponentHelper::GetActivity()` (см. `vendor/1c-native-api/include/IAndroidComponentHelper.h`),
  запуск ScannerActivity, обратный вызов с результатом →
  `BarcodeScannerAddIn::EmitScanResult()`.

## Сборка (после появления кода)

```
cmake -B build-android-arm64 ^
  -DCMAKE_TOOLCHAIN_FILE=%ANDROID_NDK%/build/cmake/android.toolchain.cmake ^
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-21 ^
  -DCMAKE_BUILD_TYPE=Release -G Ninja
cmake --build build-android-arm64
```

API level android-21 — по факту Android-проекта мобильной платформы 8.3.27
(prjandroid: minSdkVersion=21, targetSdkVersion=35). CameraX требует 21+ — совместимо.

## Риск

Формально документация 1С запрещает UI в мобильных ВК, но платформа сама
предоставляет `GetActivity()`; запуск своей Activity — обкатанный сообществом
приём. Первым делом на этом этапе собирается минимальная проба: пустая Activity
открывается и закрывается из тестового мобильного приложения.
