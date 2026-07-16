#pragma once

#if defined(__ANDROID__)

#include "AddInDefBase.h"

class BarcodeScannerAddIn;

namespace bsz::android {

enum class StartScanResult {
	Started,             // экран сканера показан, камера запускается
	PermissionRequested, // нет разрешения CAMERA — показан системный запрос
	Failed               // подробности в logcat (тег BarcodeScannerZXing)
};

// Показывает полноэкранный сканер поверх Activity 1С: превью камеры + поток
// кадров в zxing. Результаты уходят через owner->EmitScanResult из потока камеры.
StartScanResult StartScanning(IAddInDefBase* connect, BarcodeScannerAddIn* owner);

// Скрывает экран сканера и останавливает камеру. Безопасно вызывать повторно.
bool StopScanning();

} // namespace bsz::android

#endif // __ANDROID__
