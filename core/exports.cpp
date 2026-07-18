#include "ComponentBase.h"

#include "BarcodeScannerAddIn.h"

#if defined(__APPLE__)
// Диагностика подключения на устройстве (idevicesyslog): платформа не пишет
// причин отказа AttachAddIn, единственный сигнал — вызвались ли точки входа.
#include <os/log.h>
#define BSZ_TRACE(name) os_log(OS_LOG_DEFAULT, "BarcodeScannerZXing: " name " called")
#else
#define BSZ_TRACE(name)
#endif

// Сборка идёт с visibility=hidden; точки входа Native API должны остаться
// видимыми (на Windows это делает exports.def).
// На iOS ВК статически линкуется в исполняемый файл платформы, а его загрузчик
// ищет точки входа dlsym-ом: в export trie исполняемого файла ld64 помещает
// только weak-определения, поэтому weak — единственный способ попасть в trie.
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
	BSZ_TRACE("GetClassNames");
	return kClassNames;
}

extern "C" BSZ_EXPORT long GetClassObject(const WCHAR_T* /*className*/, IComponentBase** pIntf)
{
	BSZ_TRACE("GetClassObject");

	// Компонента экспортирует единственный класс — имя не анализируем.
	if (!pIntf || *pIntf)
		return 0;

	*pIntf = new BarcodeScannerAddIn();
	return 1;
}

extern "C" BSZ_EXPORT long DestroyObject(IComponentBase** pIntf)
{
	BSZ_TRACE("DestroyObject");

	if (!pIntf || !*pIntf)
		return -1;

	delete *pIntf;
	*pIntf = nullptr;
	return 0;
}

extern "C" BSZ_EXPORT AppCapabilities SetPlatformCapabilities(const AppCapabilities /*capabilities*/)
{
	BSZ_TRACE("SetPlatformCapabilities");
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
		BSZ_TRACE("RegisterLibrary");
		RegisterLibrary("BarcodeScannerZXing", nullptr, kAddinExports);
		RegisterLibrary("libBarcodeScannerZXing", nullptr, kAddinExports);
		RegisterLibrary("BarcodeScannerZXing.a", nullptr, kAddinExports);
		RegisterLibrary("libBarcodeScannerZXing.a", nullptr, kAddinExports);
	}
};

BszRegistrar g_bszRegistrar;

} // namespace

#endif // __APPLE__
