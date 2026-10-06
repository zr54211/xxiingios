#include "ComponentBase.h"

#include "BarcodeScannerAddIn.h"

// Windows и Android: точки входа Native API — экспортируемые C-функции, загрузчик
// ищет их по имени в файле библиотеки. Сборка идёт с visibility=hidden, поэтому
// они помечаются видимыми (на Windows это делает exports.def).
// iOS: все ВК статически линкуются в один исполняемый файл платформы, и
// одноимённые глобальные точки входа разных ВК конфликтуют при линковке (чужие
// сильные символы молча подменяют слабые). Поэтому здесь точки входа внутренние
// (анонимный namespace), и загрузчик получает их только через таблицу
// RegisterLibrary (см. блок __APPLE__ ниже) — как в шаблоне templateMobile.
#if defined(__APPLE__)
#define BSZ_ENTRY_POINTS_BEGIN namespace {
#define BSZ_ENTRY_POINTS_END }
#define BSZ_EXPORT
#else
#define BSZ_ENTRY_POINTS_BEGIN extern "C" {
#define BSZ_ENTRY_POINTS_END }
#if defined(_WIN32)
#define BSZ_EXPORT
#else
#define BSZ_EXPORT __attribute__((visibility("default")))
#endif
#endif

namespace {

constexpr WCHAR_T kClassNames[] = {
	u'B', u'a', u'r', u'c', u'o', u'd', u'e', u'S', u'c', u'a', u'n', u'n', u'e', u'r',
	u'Z', u'X', u'i', u'n', u'g', 0
};

} // namespace

BSZ_ENTRY_POINTS_BEGIN

BSZ_EXPORT const WCHAR_T* GetClassNames()
{
	return kClassNames;
}

BSZ_EXPORT long GetClassObject(const WCHAR_T* /*className*/, IComponentBase** pIntf)
{
	// Компонента экспортирует единственный класс — имя не анализируем.
	if (!pIntf || *pIntf)
		return 0;

	*pIntf = new BarcodeScannerAddIn();
	return 1;
}

BSZ_EXPORT long DestroyObject(IComponentBase** pIntf)
{
	if (!pIntf || !*pIntf)
		return -1;

	delete *pIntf;
	*pIntf = nullptr;
	return 0;
}

BSZ_EXPORT AppCapabilities SetPlatformCapabilities(const AppCapabilities /*capabilities*/)
{
	return eAppCapabilitiesLast;
}

BSZ_ENTRY_POINTS_END

#if defined(__APPLE__)

// iOS-загрузчик ВК не использует dlsym: статическая компонента обязана сама
// зарегистрировать таблицу точек входа через RegisterLibrary при загрузке
// приложения (шаблон templateMobile комплекта «Технология создания внешних
// компонент», include/mobile.h). Ключ сопоставления с макетом документирован
// скупо — регистрируем все разумные варианты имени, реестр это допускает.
// Имена в таблице разрешаются в точки входа из анонимного namespace выше.
extern "C" void RegisterLibrary(const char* name, const void* reserved, const void* exportTable);

namespace {

const void* kAddinExports[] = {
	"GetClassObject", (const void*)&GetClassObject,
	"DestroyObject", (const void*)&DestroyObject,
	"GetClassNames", (const void*)&GetClassNames,
	"SetPlatformCapabilities", (const void*)&SetPlatformCapabilities,
	nullptr
};

struct BszRegistrar {
	BszRegistrar()
	{
		RegisterLibrary("BarcodeScannerZXing", nullptr, kAddinExports);
		RegisterLibrary("libBarcodeScannerZXing", nullptr, kAddinExports);
		RegisterLibrary("BarcodeScannerZXing.a", nullptr, kAddinExports);
		RegisterLibrary("libBarcodeScannerZXing.a", nullptr, kAddinExports);
	}
};

BszRegistrar g_bszRegistrar;

} // namespace

#endif // __APPLE__
