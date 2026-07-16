#pragma once

#include <string>

#include "ComponentBase.h"
#include "AddInDefBase.h"
#include "IMemoryManager.h"

// Внешняя компонента сканирования штрихкодов на базе zxing-cpp.
//
// Контракт v1 (имена: русский / английский):
//   Свойства (только чтение):
//     Версия / Version             - строка "x.y.z (zxing-cpp a.b.c)"
//   Методы:
//     НачатьСканирование / StartScanning(НастройкиJSON)  - открыть экран сканера
//         НастройкиJSON: {"torch":false, "autoClose":true} (все поля необязательны)
//     ЗавершитьСканирование / StopScanning()             - закрыть экран сканера
//     ПереключитьФонарик / SetTorch(Включен)             - фонарик во время сканирования
//     РаспознатьКадр / DecodeFrame(Данные, Ширина, Высота) -> Строка JSON
//         Данные - ДвоичныеДанные: luminance-буфер (Y-плоскость), Ширина*Высота байт
//     РаспознатьИзображение / DecodeImage(Данные) -> Строка JSON  (этап B, пока заглушка)
//   События (через ВнешнееСобытие, Источник "BarcodeScannerZXing"):
//     "ScanResult"    - Данные: JSON результата распознавания
//     "ScanCancelled" - пользователь закрыл экран сканера кнопкой
class BarcodeScannerAddIn final : public IComponentBase
{
public:
	BarcodeScannerAddIn() = default;
	~BarcodeScannerAddIn() override = default;

	// IInitDoneBase
	bool ADDIN_API Init(void* connection) override;
	bool ADDIN_API setMemManager(void* memory) override;
	long ADDIN_API GetInfo() override;
	void ADDIN_API Done() override;

	// ILanguageExtenderBase
	bool ADDIN_API RegisterExtensionAs(WCHAR_T** extensionName) override;
	long ADDIN_API GetNProps() override;
	long ADDIN_API FindProp(const WCHAR_T* propName) override;
	const WCHAR_T* ADDIN_API GetPropName(long propNum, long propAlias) override;
	bool ADDIN_API GetPropVal(const long propNum, tVariant* value) override;
	bool ADDIN_API SetPropVal(const long propNum, tVariant* value) override;
	bool ADDIN_API IsPropReadable(const long propNum) override;
	bool ADDIN_API IsPropWritable(const long propNum) override;
	long ADDIN_API GetNMethods() override;
	long ADDIN_API FindMethod(const WCHAR_T* methodName) override;
	const WCHAR_T* ADDIN_API GetMethodName(const long methodNum, const long methodAlias) override;
	long ADDIN_API GetNParams(const long methodNum) override;
	bool ADDIN_API GetParamDefValue(const long methodNum, const long paramNum, tVariant* value) override;
	bool ADDIN_API HasRetVal(const long methodNum) override;
	bool ADDIN_API CallAsProc(const long methodNum, tVariant* params, const long paramCount) override;
	bool ADDIN_API CallAsFunc(const long methodNum, tVariant* retValue, tVariant* params, const long paramCount) override;

	// LocaleBase
	void ADDIN_API SetLocale(const WCHAR_T* locale) override;

	// Отправка события в 1С (ВнешнееСобытие) от платформенных слоёв (android/, ios/).
	// Потокобезопасно относительно платформы, вызывается из потока камеры/UI.
	bool EmitEvent(const char16_t* eventName, const std::string& dataUtf8);
	bool EmitScanResult(const std::string& jsonUtf8);

private:
	enum Props : long {
		ePropVersion = 0,
		ePropCount
	};

	enum Methods : long {
		eMethStartScanning = 0,
		eMethStopScanning,
		eMethDecodeFrame,
		eMethDecodeImage,
		eMethSetTorch,
		eMethCount
	};

	bool ReturnString(tVariant* value, const std::string& utf8);
	void PostError(const std::u16string& description);

	IAddInDefBase* m_connect = nullptr;
	IMemoryManager* m_memory = nullptr;
};
