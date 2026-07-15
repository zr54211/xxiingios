#pragma once

#if defined(__ANDROID__)

#include "AddInDefBase.h"

namespace bsz::android {

// Этап 2, проба UI: показать полупрозрачный полноэкранный View поверх
// текущей Activity приложения 1С (через IAndroidComponentHelper.GetActivity).
// Возвращает false с записью в logcat, если недоступен JNI/хелпер/Activity.
bool ShowOverlayProbe(IAddInDefBase* connect);

// Убрать пробный оверлей (если показан).
bool HideOverlayProbe();

} // namespace bsz::android

#endif // __ANDROID__
