#if defined(__ANDROID__)

#include "AndroidScanner.h"

#include <android/log.h>
#include <jni.h>

#include "IAndroidComponentHelper.h"

#define LOG_TAG "BarcodeScannerZXing"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace {

JavaVM* g_vm = nullptr;
jobject g_overlay = nullptr; // GlobalRef на показанный View

// Платформа грузит компоненту через System.loadLibrary -> JNI_OnLoad должен
// отработать. Если g_vm останется нулевым — фиксируем в logcat и уточняем
// способ получения JavaVM по templateMobile (см. android/README.md).
JNIEnv* Env()
{
	if (!g_vm) {
		LOGE("JavaVM is null: JNI_OnLoad was not called by the platform loader");
		return nullptr;
	}

	JNIEnv* env = nullptr;

	if (g_vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_OK)
		return env;

	if (g_vm->AttachCurrentThread(&env, nullptr) == JNI_OK)
		return env;

	LOGE("Failed to obtain JNIEnv for current thread");
	return nullptr;
}

bool ClearPendingException(JNIEnv* env, const char* where)
{
	if (!env->ExceptionCheck())
		return false;

	LOGE("Java exception in %s", where);
	env->ExceptionDescribe();
	env->ExceptionClear();
	return true;
}

bool IsMainThread(JNIEnv* env)
{
	jclass looperCls = env->FindClass("android/os/Looper");

	if (!looperCls || ClearPendingException(env, "FindClass(Looper)"))
		return false;

	const jmethodID myLooper = env->GetStaticMethodID(looperCls, "myLooper", "()Landroid/os/Looper;");
	const jmethodID mainLooper = env->GetStaticMethodID(looperCls, "getMainLooper", "()Landroid/os/Looper;");

	const jobject my = env->CallStaticObjectMethod(looperCls, myLooper);
	const jobject main = env->CallStaticObjectMethod(looperCls, mainLooper);

	return my && main && env->IsSameObject(my, main);
}

} // namespace

extern "C" JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void* /*reserved*/)
{
	g_vm = vm;
	LOGI("JNI_OnLoad: JavaVM captured");
	return JNI_VERSION_1_6;
}

namespace bsz::android {

bool ShowOverlayProbe(IAddInDefBase* connect)
{
	if (!connect)
		return false;

	if (g_overlay) {
		LOGI("Overlay already shown");
		return true;
	}

	// Мобильная платформа реализует расширенный интерфейс подключения.
	auto* connectEx = static_cast<IAddInDefBaseEx*>(connect);
	auto* helper = static_cast<IAndroidComponentHelper*>(
		connectEx->GetInterface(eIAndroidComponentHelper));

	if (!helper) {
		LOGE("IAndroidComponentHelper is not available");
		return false;
	}

	JNIEnv* env = Env();

	if (!env)
		return false;

	const jobject activity = helper->GetActivity();

	if (!activity) {
		LOGE("GetActivity() returned null");
		return false;
	}

	// Диагностика ключевой гипотезы: методы ВК вызываются в UI-потоке.
	LOGI("ShowOverlayProbe: on main thread = %s", IsMainThread(env) ? "true" : "false");

	const jclass viewCls = env->FindClass("android/view/View");
	const jmethodID viewCtor = env->GetMethodID(viewCls, "<init>", "(Landroid/content/Context;)V");
	const jobject view = env->NewObject(viewCls, viewCtor, activity);

	if (!view || ClearPendingException(env, "new View(activity)"))
		return false;

	// Полупрозрачный зелёный, чтобы проба была видна поверх формы 1С.
	const jmethodID setBg = env->GetMethodID(viewCls, "setBackgroundColor", "(I)V");
	env->CallVoidMethod(view, setBg, static_cast<jint>(0x8800CC00));

	const jclass lpCls = env->FindClass("android/view/ViewGroup$LayoutParams");
	const jmethodID lpCtor = env->GetMethodID(lpCls, "<init>", "(II)V");
	const jobject lp = env->NewObject(lpCls, lpCtor, -1, -1); // MATCH_PARENT x2

	if (!lp || ClearPendingException(env, "new LayoutParams"))
		return false;

	const jclass activityCls = env->GetObjectClass(activity);
	const jmethodID addContentView = env->GetMethodID(activityCls,
		"addContentView", "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V");

	env->CallVoidMethod(activity, addContentView, view, lp);

	if (ClearPendingException(env, "addContentView"))
		return false;

	g_overlay = env->NewGlobalRef(view);
	LOGI("Overlay shown");
	return true;
}

bool HideOverlayProbe()
{
	if (!g_overlay)
		return true;

	JNIEnv* env = Env();

	if (!env)
		return false;

	const jclass viewCls = env->GetObjectClass(g_overlay);
	const jmethodID getParent = env->GetMethodID(viewCls, "getParent", "()Landroid/view/ViewParent;");
	const jobject parent = env->CallObjectMethod(g_overlay, getParent);

	if (parent && !ClearPendingException(env, "getParent")) {

		const jclass groupCls = env->FindClass("android/view/ViewGroup");

		if (env->IsInstanceOf(parent, groupCls)) {

			const jmethodID removeView = env->GetMethodID(groupCls,
				"removeView", "(Landroid/view/View;)V");
			env->CallVoidMethod(parent, removeView, g_overlay);
			ClearPendingException(env, "removeView");

		}

	}

	env->DeleteGlobalRef(g_overlay);
	g_overlay = nullptr;
	LOGI("Overlay hidden");
	return true;
}

} // namespace bsz::android

#endif // __ANDROID__
