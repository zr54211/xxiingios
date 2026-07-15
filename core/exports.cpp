#include "ComponentBase.h"

#include "BarcodeScannerAddIn.h"

// Сборка идёт с visibility=hidden; точки входа Native API должны остаться
// видимыми (на Windows это делает exports.def).
#if defined(_WIN32)
#define BSZ_EXPORT
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
