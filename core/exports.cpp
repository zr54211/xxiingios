#include "ComponentBase.h"

#include "BarcodeScannerAddIn.h"

// Сборка идёт с visibility=hidden; точки входа Native API должны остаться
// видимыми (на Windows это делает exports.def).
// На iOS ВК статически линкуется в исполняемый файл платформы; рабочий механизм
// подключения — саморегистрация через RegisterLibrary (см. блок __APPLE__ ниже).
// weak-атрибут дополнительно кладёт точки входа в export trie исполняемого файла
// (ld64 помещает туда только weak-определения) — подстраховка, не основной путь.
#if defined(_WIN32)
#define BSZ_EXPORT
#elif defined(__APPLE__)
#define BSZ_EXPORT __attribute__((visibility("default"), weak))
#else
#define BSZ_EXPORT __attribute__((visibility("default")))
#endif

namespace {

constexpr WCHAR_T kClassNames[] = {
	u'B', u'a', u'r', u'c', u'o', u'd', u'e', u'S', u'c', u'a', u'n', u'n', u'e', u'r',
	u'Z', u'X', u'i', u'n', u'g', 0
};

} // namespace

extern "C" BSZ_EXPORT const WCHAR_T* GetClassNames()
{
	return kClassNames;
}

extern "C" BSZ_EXPORT long GetClassObject(const WCHAR_T* /*className*/, IComponentBase** pIntf)
{
	// Компонента экспортирует единственный класс — имя не анализируем.
	if (!pIntf || *pIntf)
		return 0;

	*pIntf = new BarcodeScannerAddIn();
	return 1;
}

extern "C" BSZ_EXPORT long DestroyObject(IComponentBase** pIntf)
{
	if (!pIntf || !*pIntf)
		return -1;

	delete *pIntf;
	*pIntf = nullptr;
	return 0;
}

extern "C" BSZ_EXPORT AppCapabilities SetPlatformCapabilities(const AppCapabilities /*capabilities*/)
{
	return eAppCapabilitiesLast;
}

#if defined(__APPLE__)

// iOS-загрузчик ВК не использует dlsym: статическая компонента обязана сама
// зарегистрировать таблицу точек входа через RegisterLibrary при загрузке
// приложения (шаблон templateMobile комплекта «Технология создания внешних
// компонент», include/mobile.h). Ключ сопоставления с макетом документирован
// скупо — регистрируем все разумные варианты имени, реестр это допускает.
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
