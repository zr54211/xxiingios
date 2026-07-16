#pragma once

#if defined(__ANDROID__)

#include <string>

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
// settingsJson: {"torch":false, "autoClose":true} — поля необязательны.
StartScanResult StartScanning(IAddInDefBase* connect, BarcodeScannerAddIn* owner,
	const std::string& settingsJson);

// Скрывает экран сканера и останавливает камеру. Безопасно вызывать повторно.
bool StopScanning();

// Фонарик во время сканирования.
bool SetTorch(bool on);

} // namespace bsz::android

#endif // __ANDROID__
