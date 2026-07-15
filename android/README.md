# Android-слой (этап 2)

## Архитектура

Ключевое ограничение: приложение собирает сборщик 1С, поэтому мы **не можем**
добавлять Java/androidx-зависимости (CameraX) или объявлять свои Activity в
AndroidManifest. Всё живёт внутри нашего `.so`:

- **Камера — NDK Camera2 API** (`libcamera2ndk` + `libmediandk`, API 24+):
  открытие камеры, capture session, `AImageReader` (YUV_420_888, 1080p+),
  continuous autofocus, tap-to-focus, зум, фонарик — целиком из C++.
  Y-плоскость кадра → `bsz::DecodeLuminance` → `EmitScanResult`.
- **Экран сканера — не Activity, а полноэкранный View** поверх текущей
  Activity приложения 1С: `IAndroidComponentHelper::GetActivity()` → JNI →
  `runOnUiThread` → `addContentView` (SurfaceView для preview + кнопки).
  Закрытие — удаление View. Регистрация в манифесте не требуется.
- Разрешение CAMERA: должно быть у приложения (сборщик 1С добавляет его при
  использовании мультимедиа; у Мобильной кассы уже есть). Проверка/запрос
  runtime-разрешения — через JNI (`checkSelfPermission`/`requestPermissions`).

Сборка с `ANDROID_PLATFORM=android-21` (как у платформы 8.3.27, prjandroid:
minSdkVersion=21), а `libcamera2ndk` подгружается через `dlopen` в рантайме:
на устройствах ниже Android 7.0 (API 24) компонента подключится, но
`НачатьСканирование` вернёт понятную ошибку; режим B работает везде.

## Порядок работ

1. Проба UI: пустой полноэкранный View через GetActivity/addContentView,
   открыть/закрыть из BSL (валидация риска «UI из ВК»).
2. Camera2: preview + поток кадров в декодер.
3. Эргономика: рамка, фонарик, tap-to-focus, зум, вибро при успехе.

## Сборка

```
cmake -B build-android-arm64 ^
  -DCMAKE_TOOLCHAIN_FILE=%ANDROID_NDK%/build/cmake/android.toolchain.cmake ^
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-21 ^
  -DCMAKE_BUILD_TYPE=Release -G Ninja
cmake --build build-android-arm64
```

NDK: r27 LTS (устанавливается в `%LOCALAPPDATA%\Android\Sdk\ndk\27.3.13750724`).
