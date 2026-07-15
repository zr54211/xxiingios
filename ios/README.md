# iOS-слой (этап 3)

Здесь появится платформенная часть для iOS (Objective-C++):

- **ScannerViewController**: полноэкранный сканер — AVFoundation
  (`AVCaptureSession`, preview layer, video output 1080p+), continuous autofocus,
  tap-to-focus, зум, фонарик. Y-плоскость `CVPixelBuffer` → `bsz::DecodeLuminance`.
- Показ поверх окна 1С: `rootViewController.presentViewController` из главного
  потока; результат → `BarcodeScannerAddIn::EmitScanResult()`.

## Сборка (после появления кода)

Статическая библиотека arm64 через CMake + Xcode toolchain
(`cmake -G Xcode -DCMAKE_SYSTEM_NAME=iOS ...`), линкуется в приложение
сборщиком мобильных приложений 1С.

## Риск №1 проекта

В отличие от Android, у платформы нет хелпера для доступа к UI на iOS, и
прецедентов 1С-ВК с собственным экраном не найдено. **Первая задача этапа —
минимальная проба: показать пустой ViewController из ВК в собранном мобильном
приложении.** Если проба провалится — на iOS остаётся режим B
(`РаспознатьИзображение` по фото от `СредстваМультимедиа.СделатьФотоснимок`).
