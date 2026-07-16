#pragma once

#if defined(__ANDROID__)

#include <functional>
#include <string>

struct ANativeWindow;

namespace bsz::android {

// Колбэк с JSON результата распознавания ({"found":..,"barcodes":[..]}) и
// нормированными углами первого кода в координатах кадра анализа (8 float).
// Вызывается из потока обработки кадров.
using ScanEmitFn = std::function<void(const std::string& jsonUtf8, const float* points8)>;

// Открывает заднюю камеру (NDK Camera2 через dlopen, требует API 24+) и
// запускает preview в переданную поверхность + поток кадров YUV в zxing.
// width/height — верхняя граница разрешения потока анализа: берётся лучший
// поддерживаемый камерой YUV-размер, не превышающий её.
// previewWindow должен жить до CameraStop (владение остаётся у вызывающего).
bool CameraStart(ANativeWindow* previewWindow, int width, int height, ScanEmitFn emit);

// Останавливает съёмку и освобождает ресурсы камеры. Безопасно вызывать повторно.
void CameraStop();

// Фонарик во время сканирования. false — если сканер не запущен или вспышки нет.
bool CameraSetTorch(bool on);

// Фокусировка в точке превью (нормированные координаты вида 0..1).
void CameraFocusAt(float nx, float ny);

// Перевод нормированной точки кадра анализа в нормированные координаты
// вида превью (учитывает ориентацию сенсора).
void CameraFrameToView(float fx, float fy, float& nx, float& ny);

} // namespace bsz::android

#endif // __ANDROID__
