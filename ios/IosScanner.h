#pragma once

#if defined(__APPLE__)

#include <string>

#include "AddInDefBase.h"

class BarcodeScannerAddIn;

namespace bsz::ios {

enum class StartScanResult {
	Started, // оверлей сканера показан (или поставлен в очередь главного потока)
	Failed   // подробности в системном логе (тег BarcodeScannerZXing, см. idevicesyslog)
};

// Этап 0 (прототип риска UI): показывает полноэкранный оверлей поверх окна 1С.
// Камеры ещё нет — кнопка закрытия шлёт ScanCancelled, проверяя доставку
// событий в BSL. settingsJson пока не используется.
StartScanResult StartScanning(IAddInDefBase* connect, BarcodeScannerAddIn* owner,
	const std::string& settingsJson);

// Скрывает оверлей. Безопасно вызывать повторно.
bool StopScanning();

// Фонарик — появится вместе с камерой (этап 1).
bool SetTorch(bool on);

} // namespace bsz::ios

#endif // __APPLE__
