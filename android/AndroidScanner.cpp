#if defined(__ANDROID__)

#include "AndroidScanner.h"

#include <android/log.h>
#include <jni.h>

#include <functional>
#include <memory>

#include "IAndroidComponentHelper.h"

#define LOG_TAG "BarcodeScannerZXing"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace {

JavaVM* g_vm = nullptr;
jobject g_overlay = nullptr;   // GlobalRef на показанный View
jobject g_activity = nullptr;  // GlobalRef на Activity 1С (сохраняется при первом показе)
jclass g_bridgeCls = nullptr;  // GlobalRef на com.onecvn.addin.scanner.UiBridge
jmethodID g_bridgePost = nullptr;

// Задача, исполняемая в UI-потоке через UiBridge.post -> runOnUiThread -> nativeRun.
struct UiTask {
	std::function<void()> fn;
};

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

void JNICALL NativeRun(JNIEnv* /*env*/, jclass /*cls*/, jlong ctx)
{
	std::unique_ptr<UiTask> task(reinterpret_cast<UiTask*>(ctx));
	task->fn();
}

// Находит UiBridge из Java-части компоненты (apk) и регистрирует nativeRun.
bool EnsureBridge(JNIEnv* env, IAndroidComponentHelper* helper)
{
	if (g_bridgeCls)
		return true;

	static const char16_t kClassName[] = u"com.onecvn.addin.scanner.UiBridge";
	const jclass cls = helper->FindClass(reinterpret_cast<const WCHAR_T*>(kClassName));

	if (!cls || ClearPendingException(env, "FindClass(UiBridge)")) {
		LOGE("UiBridge class not found: java part (apk) is missing from the bundle?");
		return false;
	}

	static const JNINativeMethod methods[] = {
		{"nativeRun", "(J)V", reinterpret_cast<void*>(&NativeRun)},
	};

	if (env->RegisterNatives(cls, methods, 1) != JNI_OK || ClearPendingException(env, "RegisterNatives")) {
		LOGE("RegisterNatives(UiBridge.nativeRun) failed");
		return false;
	}

	g_bridgePost = env->GetStaticMethodID(cls, "post", "(Landroid/app/Activity;J)V");

	if (!g_bridgePost || ClearPendingException(env, "GetStaticMethodID(post)"))
		return false;

	g_bridgeCls = static_cast<jclass>(env->NewGlobalRef(cls));
	LOGI("UiBridge ready");
	return true;
}

bool RunOnUiThread(JNIEnv* env, std::function<void()> fn)
{
	if (!g_bridgeCls || !g_activity) {
		LOGE("RunOnUiThread: bridge is not initialized");
		return false;
	}

	auto task = std::make_unique<UiTask>();
	task->fn = std::move(fn);

	env->CallStaticVoidMethod(g_bridgeCls, g_bridgePost, g_activity,
		reinterpret_cast<jlong>(task.get()));

	if (ClearPendingException(env, "UiBridge.post"))
		return false;

	task.release(); // владение перешло к NativeRun
	return true;
}

// Тело пробы: выполняется строго в UI-потоке.
void ShowOverlayOnUiThread()
{
	JNIEnv* env = Env();

	if (!env || g_overlay)
		return;

	LOGI("ShowOverlayOnUiThread: on main thread = %s", IsMainThread(env) ? "true" : "false");

	const jclass viewCls = env->FindClass("android/view/View");
	const jmethodID viewCtor = env->GetMethodID(viewCls, "<init>", "(Landroid/content/Context;)V");
	const jobject view = env->NewObject(viewCls, viewCtor, g_activity);

	if (!view || ClearPendingException(env, "new View(activity)"))
		return;

	// Полупрозрачный зелёный, чтобы проба была видна поверх формы 1С.
	const jmethodID setBg = env->GetMethodID(viewCls, "setBackgroundColor", "(I)V");
	env->CallVoidMethod(view, setBg, static_cast<jint>(0x8800CC00));

	const jclass lpCls = env->FindClass("android/view/ViewGroup$LayoutParams");
	const jmethodID lpCtor = env->GetMethodID(lpCls, "<init>", "(II)V");
	const jobject lp = env->NewObject(lpCls, lpCtor, -1, -1); // MATCH_PARENT x2

	if (!lp || ClearPendingException(env, "new LayoutParams"))
		return;

	const jclass activityCls = env->GetObjectClass(g_activity);
	const jmethodID addContentView = env->GetMethodID(activityCls,
		"addContentView", "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V");

	env->CallVoidMethod(g_activity, addContentView, view, lp);

	if (ClearPendingException(env, "addContentView"))
		return;

	g_overlay = env->NewGlobalRef(view);
	LOGI("Overlay shown");
}

void HideOverlayOnUiThread()
{
	JNIEnv* env = Env();

	if (!env || !g_overlay)
		return;

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

	if (!g_activity) {

		const jobject activity = helper->GetActivity();

		if (!activity) {
			LOGE("GetActivity() returned null");
			return false;
		}

		g_activity = env->NewGlobalRef(activity);

	}

	if (!EnsureBridge(env, helper))
		return false;

	return RunOnUiThread(env, ShowOverlayOnUiThread);
}

bool HideOverlayProbe()
{
	if (!g_overlay)
		return true;

	JNIEnv* env = Env();

	if (!env)
		return false;

	return RunOnUiThread(env, HideOverlayOnUiThread);
}

} // namespace bsz::android

#endif // __ANDROID__
