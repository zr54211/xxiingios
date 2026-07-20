#pragma once

#if defined(__ANDROID__)

#include <functional>
#include <string>

struct ANativeWindow;

namespace bsz::android {

// Колбэк с JSON результата распознавания ({"found":..,"barcodes":[..]}).
// Вызывается из потока обработки кадров (с дедупликацией повторов).
using ScanEmitFn = std::function<void(const std::string& jsonUtf8)>;

// Колбэк сопровождения: углы первого кода в нормированных координатах кадра
// (8 float) на каждом распознанном кадре; nullptr — код ушёл из кадра.
using MarkerFn = std::function<void(const float* points8OrNull)>;

// Колбэк со стоп-кадром: цветной ARGB-кадр (0xAARRGGBB), на котором распознан
// код, уже в ориентации вида превью. Вызывается перед ScanEmitFn того же кадра.
using FrozenFrameFn = std::function<void(const uint32_t* argb, int width, int height)>;

// Открывает заднюю камеру (NDK Camera2 через dlopen, требует API 24+) и
// запускает preview в переданную поверхность + поток кадров YUV в zxing.
// width/height — верхняя граница разрешения потока анализа: берётся лучший
// поддерживаемый камерой YUV-размер, не превышающий её.
// previewWindow должен жить до CameraStop (владение остаётся у вызывающего).
bool CameraStart(ANativeWindow* previewWindow, int width, int height,
	ScanEmitFn emit, MarkerFn markers, FrozenFrameFn frozen);

// Останавливает съёмку и освобождает ресурсы камеры. Безопасно вызывать повторно.
void CameraStop();

// Останавливает подачу кадров, не разрушая сессию: поверхность превью остаётся
// с последним кадром на экране до закрытия оверлея. Вызывать только из emit-коллбека
// CameraStart — он выполняется под внутренним мьютексом камеры, из других потоков
// состояние сессии не защищено.
void CameraFreezePreview();

// Фонарик во время сканирования. false — если сканер не запущен или вспышки нет.
bool CameraSetTorch(bool on);

// Фокусировка в точке превью (нормированные координаты вида 0..1).
void CameraFocusAt(float nx, float ny);

// Перевод нормированной точки кадра анализа в нормированные координаты
// вида превью (учитывает ориентацию сенсора).
void CameraFrameToView(float fx, float fy, float& nx, float& ny);

} // namespace bsz::android

#endif // __ANDROID__
