#include "BarcodeScannerAddIn.h"

#include <array>

#include "Decoder.h"
#include "StringUtils.h"

#if defined(__ANDROID__)
#include "AndroidScanner.h"
#endif

namespace {

constexpr char16_t kExtensionName[] = u"BarcodeScannerZXing";
constexpr char16_t kEventSource[]   = u"BarcodeScannerZXing";
constexpr char16_t kEventScan[]     = u"ScanResult";

constexpr char kComponentVersion[] = "0.1.0";

// Имена: [0] - английский (международный) синоним, [1] - русский.
constexpr std::array<std::array<const char16_t*, 2>, 1> kPropNames = {{
	{u"Version", u"Версия"},
}};

constexpr std::array<std::array<const char16_t*, 2>, 5> kMethodNames = {{
	{u"StartScanning", u"НачатьСканирование"},
	{u"StopScanning",  u"ЗавершитьСканирование"},
	{u"DecodeFrame",   u"РаспознатьКадр"},
	{u"DecodeImage",   u"РаспознатьИзображение"},
	{u"SetTorch",      u"ПереключитьФонарик"},
}};

constexpr long kMethodParamCount[] = {1, 0, 3, 1, 1};
constexpr bool kMethodHasRet[]     = {false, false, true, true, false};

bool EqualNames(const WCHAR_T* left, const char16_t* right)
{
	if (!left || !right)
		return false;

	size_t i = 0;

	while (left[i] != 0 && right[i] != 0) {
		// Регистронезависимо для латиницы и кириллицы (диапазон U+0410..U+044F).
		char16_t l = static_cast<char16_t>(left[i]);
		char16_t r = right[i];

		if (l >= u'A' && l <= u'Z') l += 32;
		if (r >= u'A' && r <= u'Z') r += 32;
		if (l >= u'А' && l <= u'Я') l += 32;
		if (r >= u'А' && r <= u'Я') r += 32;

		if (l != r)
			return false;

		++i;
	}

	return left[i] == 0 && right[i] == 0;
}

} // namespace

bool ADDIN_API BarcodeScannerAddIn::Init(void* connection)
{
	m_connect = static_cast<IAddInDefBase*>(connection);
	return m_connect != nullptr;
}

bool ADDIN_API BarcodeScannerAddIn::setMemManager(void* memory)
{
	m_memory = static_cast<IMemoryManager*>(memory);
	return m_memory != nullptr;
}

long ADDIN_API BarcodeScannerAddIn::GetInfo()
{
	return 2000;
}

void ADDIN_API BarcodeScannerAddIn::Done()
{
#if defined(__ANDROID__)
	bsz::android::StopScanning();
#endif
	m_connect = nullptr;
	m_memory = nullptr;
}

bool ADDIN_API BarcodeScannerAddIn::RegisterExtensionAs(WCHAR_T** extensionName)
{
	return bsz::AllocWString(m_memory, kExtensionName, extensionName);
}

long ADDIN_API BarcodeScannerAddIn::GetNProps()
{
	return ePropCount;
}

long ADDIN_API BarcodeScannerAddIn::FindProp(const WCHAR_T* propName)
{
	for (long i = 0; i < ePropCount; ++i) {

		if (EqualNames(propName, kPropNames[i][0]) || EqualNames(propName, kPropNames[i][1]))
			return i;

	}

	return -1;
}

const WCHAR_T* ADDIN_API BarcodeScannerAddIn::GetPropName(long propNum, long propAlias)
{
	if (propNum < 0 || propNum >= ePropCount || propAlias < 0 || propAlias > 1)
		return nullptr;

	WCHAR_T* result = nullptr;

	if (!bsz::AllocWString(m_memory, kPropNames[propNum][propAlias], &result))
		return nullptr;

	return result;
}

bool ADDIN_API BarcodeScannerAddIn::GetPropVal(const long propNum, tVariant* value)
{
	if (propNum == ePropVersion) {

		const std::string version =
			std::string(kComponentVersion) + " (zxing-cpp " + bsz::ZXingVersion() + ")";
		return ReturnString(value, version);

	}

	return false;
}

bool ADDIN_API BarcodeScannerAddIn::SetPropVal(const long /*propNum*/, tVariant* /*value*/)
{
	return false;
}

bool ADDIN_API BarcodeScannerAddIn::IsPropReadable(const long propNum)
{
	return propNum >= 0 && propNum < ePropCount;
}

bool ADDIN_API BarcodeScannerAddIn::IsPropWritable(const long /*propNum*/)
{
	return false;
}

long ADDIN_API BarcodeScannerAddIn::GetNMethods()
{
	return eMethCount;
}

long ADDIN_API BarcodeScannerAddIn::FindMethod(const WCHAR_T* methodName)
{
	for (long i = 0; i < eMethCount; ++i) {

		if (EqualNames(methodName, kMethodNames[i][0]) || EqualNames(methodName, kMethodNames[i][1]))
			return i;

	}

	return -1;
}

const WCHAR_T* ADDIN_API BarcodeScannerAddIn::GetMethodName(const long methodNum, const long methodAlias)
{
	if (methodNum < 0 || methodNum >= eMethCount || methodAlias < 0 || methodAlias > 1)
		return nullptr;

	WCHAR_T* result = nullptr;

	if (!bsz::AllocWString(m_memory, kMethodNames[methodNum][methodAlias], &result))
		return nullptr;

	return result;
}

long ADDIN_API BarcodeScannerAddIn::GetNParams(const long methodNum)
{
	if (methodNum < 0 || methodNum >= eMethCount)
		return 0;

	return kMethodParamCount[methodNum];
}

bool ADDIN_API BarcodeScannerAddIn::GetParamDefValue(const long methodNum, const long paramNum, tVariant* value)
{
	// НачатьСканирование(НастройкиJSON = "") — настройки можно не передавать.
	if (methodNum == eMethStartScanning && paramNum == 0) {

		if (!bsz::AllocWString(m_memory, u"", &value->pwstrVal))
			return false;

		TV_VT(value) = VTYPE_PWSTR;
		value->wstrLen = 0;
		return true;

	}

	return false;
}

bool ADDIN_API BarcodeScannerAddIn::HasRetVal(const long methodNum)
{
	if (methodNum < 0 || methodNum >= eMethCount)
		return false;

	return kMethodHasRet[methodNum];
}

bool ADDIN_API BarcodeScannerAddIn::CallAsProc(const long methodNum, tVariant* params, const long paramCount)
{
	switch (methodNum) {
	case eMethStartScanning: {
#if defined(__ANDROID__)
		std::string settings;

		if (paramCount >= 1 && params && TV_VT(&params[0]) == VTYPE_PWSTR && params[0].pwstrVal)
			settings = bsz::ToUtf8(params[0].pwstrVal, params[0].wstrLen);

		switch (bsz::android::StartScanning(m_connect, this, settings)) {
		case bsz::android::StartScanResult::Started:
			return true;

		case bsz::android::StartScanResult::PermissionRequested:
			PostError(u"НачатьСканирование: нет разрешения на камеру — показан системный запрос,"
				u" повторите вызов после выдачи разрешения");
			return false;

		default:
			PostError(u"НачатьСканирование: не удалось запустить сканер (см. logcat BarcodeScannerZXing)");
			return false;
		}
#else
		PostError(u"НачатьСканирование: экран сканера ещё не реализован для этой ОС");
		return false;
#endif
	}

	case eMethSetTorch:
#if defined(__ANDROID__)
		if (paramCount < 1 || !params || TV_VT(&params[0]) != VTYPE_BOOL) {
			PostError(u"ПереключитьФонарик: параметр должен быть булевым");
			return false;
		}

		if (!bsz::android::SetTorch(TV_BOOL(&params[0]))) {
			PostError(u"ПереключитьФонарик: фонарик недоступен или сканер не запущен");
			return false;
		}

		return true;
#else
		PostError(u"ПереключитьФонарик: не реализовано для этой ОС");
		return false;
#endif

	case eMethStopScanning:
#if defined(__ANDROID__)
		return bsz::android::StopScanning();
#else
		PostError(u"ЗавершитьСканирование: экран сканера ещё не реализован для этой ОС");
		return false;
#endif

	default:
		return false;
	}
}

bool ADDIN_API BarcodeScannerAddIn::CallAsFunc(const long methodNum, tVariant* retValue,
	tVariant* params, const long paramCount)
{
	switch (methodNum) {
	case eMethDecodeFrame: {

		if (paramCount != 3 || !params)
			return false;

		if (TV_VT(&params[0]) != VTYPE_BLOB)
			return ReturnString(retValue,
				R"({"found":0,"barcodes":[],"error":"param 1 must be binary data"})");

		if (TV_VT(&params[1]) != VTYPE_I4 || TV_VT(&params[2]) != VTYPE_I4)
			return ReturnString(retValue,
				R"({"found":0,"barcodes":[],"error":"params 2,3 must be numbers"})");

		const int width  = static_cast<int>(TV_I4(&params[1]));
		const int height = static_cast<int>(TV_I4(&params[2]));

		if (width <= 0 || height <= 0
			|| params[0].strLen < static_cast<uint32_t>(width) * static_cast<uint32_t>(height))
			return ReturnString(retValue,
				R"({"found":0,"barcodes":[],"error":"buffer size does not match width*height"})");

		const std::string json = bsz::DecodeLuminance(
			reinterpret_cast<const uint8_t*>(params[0].pstrVal), width, height);
		return ReturnString(retValue, json);

	}

	case eMethDecodeImage:
		// Этап B: декодирование JPEG/PNG (фото от СредстваМультимедиа.СделатьФотоснимок).
		// Потребуется распаковка изображения (план: stb_image).
		return ReturnString(retValue,
			R"({"found":0,"barcodes":[],"error":"not implemented yet"})");

	default:
		return false;
	}
}

void ADDIN_API BarcodeScannerAddIn::SetLocale(const WCHAR_T* /*locale*/)
{
}

bool BarcodeScannerAddIn::EmitEvent(const char16_t* eventName, const std::string& dataUtf8)
{
	if (!m_connect)
		return false;

	const std::u16string data16 = bsz::ToU16(dataUtf8);

	return m_connect->ExternalEvent(
		const_cast<WCHAR_T*>(reinterpret_cast<const WCHAR_T*>(kEventSource)),
		const_cast<WCHAR_T*>(reinterpret_cast<const WCHAR_T*>(eventName)),
		const_cast<WCHAR_T*>(reinterpret_cast<const WCHAR_T*>(data16.c_str())));
}

bool BarcodeScannerAddIn::EmitScanResult(const std::string& jsonUtf8)
{
	return EmitEvent(kEventScan, jsonUtf8);
}

bool BarcodeScannerAddIn::ReturnString(tVariant* value, const std::string& utf8)
{
	if (!value)
		return false;

	const std::u16string text = bsz::ToU16(utf8);

	if (!bsz::AllocWString(m_memory, text, &value->pwstrVal))
		return false;

	TV_VT(value) = VTYPE_PWSTR;
	value->wstrLen = static_cast<uint32_t>(text.size());
	return true;
}

void BarcodeScannerAddIn::PostError(const std::u16string& description)
{
	if (!m_connect)
		return;

	// ADDIN_E_FAIL = 1006 (см. AddInDefBase.h), wcode 0 — текстовая ошибка компоненты.
	m_connect->AddError(0,
		reinterpret_cast<const WCHAR_T*>(kExtensionName),
		reinterpret_cast<const WCHAR_T*>(description.c_str()),
		1006);
}
