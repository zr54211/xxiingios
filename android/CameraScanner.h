#pragma once

#if defined(__ANDROID__)

#include <functional>
#include <string>

struct ANativeWindow;

namespace bsz::android {

// Колбэк с JSON результата распознавания ({"found":..,"barcodes":[..]}).
// Вызывается из потока обработки кадров.
using ScanEmitFn = std::function<void(const std::string& jsonUtf8)>;

// Открывает заднюю камеру (NDK Camera2 через dlopen, требует API 24+) и
// запускает preview в переданную поверхность + поток кадров YUV в zxing.
// width/height — верхняя граница разрешения потока анализа: берётся лучший
// поддерживаемый камерой YUV-размер, не превышающий её.
// previewWindow должен жить до CameraStop (владение остаётся у вызывающего).
bool CameraStart(ANativeWindow* previewWindow, int width, int height, ScanEmitFn emit);

// Останавливает съёмку и освобождает ресурсы камеры. Безопасно вызывать повторно.
void CameraStop();

} // namespace bsz::android

#endif // __ANDROID__
