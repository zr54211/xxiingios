#if defined(__ANDROID__)

#include "AndroidScanner.h"

#include <android/log.h>
#include <android/native_window_jni.h>
#include <jni.h>

#include <array>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

#include "BarcodeScannerAddIn.h"
#include "CameraScanner.h"
#include "IAndroidComponentHelper.h"

#define LOG_TAG "BarcodeScannerZXing"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace {

// Верхняя граница разрешения потока анализа (реальный размер выбирается
// из поддерживаемых камерой). Плотные QR при 720p не читаются стабильно.
constexpr int kFrameWidth = 1920;
constexpr int kFrameHeight = 1080;

JavaVM* g_vm = nullptr;
jobject g_activity = nullptr; // GlobalRef на Activity 1С

// UiBridge: диспетчеризация в UI-поток.
jclass g_bridgeCls = nullptr;
jmethodID g_bridgePost = nullptr;

// ScannerOverlay: экран сканера и разрешение камеры.
jclass g_overlayCls = nullptr;
jmethodID g_overlayShow = nullptr;
jmethodID g_overlayHide = nullptr;
jmethodID g_overlayHasPermission = nullptr;
jmethodID g_overlayRequestPermission = nullptr;
jmethodID g_overlayShowMarkers = nullptr;

ANativeWindow* g_previewWindow = nullptr; // живёт между onSurface(create/destroy)

std::mutex g_ownerMutex;
BarcodeScannerAddIn* g_owner = nullptr;

// Настройки текущего сеанса сканирования (из НастройкиJSON).
bool g_autoClose = true;
bool g_torchOnStart = false;
bool g_torchOn = false; // текущее состояние для кнопки на оверлее

// Примитивный разбор булева поля плоского JSON настроек.
bool JsonFlag(const std::string& json, const char* key, bool defaultValue)
{
	const std::string needle = std::string("\"") + key + "\"";
	const size_t keyPos = json.find(needle);

	if (keyPos == std::string::npos)
		return defaultValue;

	const size_t colon = json.find(':', keyPos + needle.size());

	if (colon == std::string::npos)
		return defaultValue;

	const size_t value = json.find_first_not_of(" \t\r\n", colon + 1);

	if (value == std::string::npos)
		return defaultValue;

	return json.compare(value, 4, "true") == 0;
}

// Задача, исполняемая в UI-потоке через UiBridge.post -> runOnUiThread -> nativeRun.
struct UiTask {
	std::function<void()> fn;
};

// Платформа грузит компоненту через System.loadLibrary -> JNI_OnLoad должен
// отработать. Если g_vm останется нулевым — фиксируем в logcat.
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

void JNICALL NativeRun(JNIEnv* /*env*/, jclass /*cls*/, jlong ctx)
{
	std::unique_ptr<UiTask> task(reinterpret_cast<UiTask*>(ctx));
	task->fn();
}

bool RunOnUiThread(JNIEnv* env, std::function<void()> fn);

// Вызывается в UI-потоке при появлении (surface != null) и уничтожении
// (surface == null) поверхности превью из ScannerOverlay.
void JNICALL NativeOnSurface(JNIEnv* env, jclass /*cls*/, jobject surface)
{
	if (!surface) {

		bsz::android::CameraStop();

		if (g_previewWindow) {
			ANativeWindow_release(g_previewWindow);
			g_previewWindow = nullptr;
		}

		return;
	}

	if (g_previewWindow) {
		// surfaceChanged может приходить повторно — камера уже работает.
		return;
	}

	g_previewWindow = ANativeWindow_fromSurface(env, surface);

	if (!g_previewWindow) {
		LOGE("ANativeWindow_fromSurface returned null");
		return;
	}

	const bool started = bsz::android::CameraStart(g_previewWindow, kFrameWidth, kFrameHeight,
		[](const std::string& json) {

			{
				std::lock_guard<std::mutex> lock(g_ownerMutex);

				if (g_owner)
					g_owner->EmitScanResult(json);

			}

			if (g_autoClose) {
				// Даём рамке вокруг кода мелькнуть перед закрытием экрана.
				std::this_thread::sleep_for(std::chrono::milliseconds(400));
				bsz::android::StopScanning();
			}

		},
		[](const float* points) {

			// Сопровождение кода рамкой: координаты кадра -> вида; nullptr — погасить.
			const bool visible = points != nullptr;
			std::array<float, 8> view{};

			if (visible)
				for (int i = 0; i < 4; ++i)
					bsz::android::CameraFrameToView(points[i * 2], points[i * 2 + 1],
						view[i * 2], view[i * 2 + 1]);

			JNIEnv* env = Env();

			if (!env)
				return;

			RunOnUiThread(env, [view, visible] {

				JNIEnv* uiEnv = Env();

				if (!uiEnv)
					return;

				jfloatArray arr = nullptr;

				if (visible) {
					arr = uiEnv->NewFloatArray(8);
					uiEnv->SetFloatArrayRegion(arr, 0, 8, view.data());
				}

				uiEnv->CallStaticVoidMethod(g_overlayCls, g_overlayShowMarkers, arr);
				ClearPendingException(uiEnv, "showMarkers");

				if (arr)
					uiEnv->DeleteLocalRef(arr);

			});

		});

	if (!started) {
		ANativeWindow_release(g_previewWindow);
		g_previewWindow = nullptr;
		return;
	}

	g_torchOn = g_torchOnStart && bsz::android::CameraSetTorch(true);
}

// Тап по превью (UI-поток): фокусировка в точке.
void JNICALL NativeOnTap(JNIEnv* /*env*/, jclass /*cls*/, jfloat nx, jfloat ny)
{
	bsz::android::CameraFocusAt(nx, ny);
}

// Кнопка фонарика на оверлее (UI-поток): переключение.
void JNICALL NativeOnTorch(JNIEnv* /*env*/, jclass /*cls*/)
{
	g_torchOn = !g_torchOn && bsz::android::CameraSetTorch(true);

	if (!g_torchOn)
		bsz::android::CameraSetTorch(false);
}

// Кнопка закрытия на оверлее (UI-поток).
void JNICALL NativeOnClose(JNIEnv* /*env*/, jclass /*cls*/)
{
	{
		std::lock_guard<std::mutex> lock(g_ownerMutex);

		if (g_owner)
			g_owner->EmitEvent(u"ScanCancelled", "");

	}

	bsz::android::StopScanning();
}

jclass FindComponentClass(JNIEnv* env, IAndroidComponentHelper* helper, const char16_t* name)
{
	const jclass cls = helper->FindClass(reinterpret_cast<const WCHAR_T*>(name));

	if (!cls || ClearPendingException(env, "FindClass"))
		return nullptr;

	return cls;
}

// Находит классы Java-части компоненты (apk) и регистрирует нативные колбэки.
bool EnsureJavaPart(JNIEnv* env, IAndroidComponentHelper* helper)
{
	if (g_bridgeCls && g_overlayCls)
		return true;

	const jclass bridge = FindComponentClass(env, helper, u"com.onecvn.addin.scanner.UiBridge");
	const jclass overlay = FindComponentClass(env, helper, u"com.onecvn.addin.scanner.ScannerOverlay");

	if (!bridge || !overlay) {
		LOGE("Java part classes not found: apk is missing from the bundle?");
		return false;
	}

	static const JNINativeMethod bridgeMethods[] = {
		{"nativeRun", "(J)V", reinterpret_cast<void*>(&NativeRun)},
	};
	static const JNINativeMethod overlayMethods[] = {
		{"onSurface", "(Landroid/view/Surface;)V", reinterpret_cast<void*>(&NativeOnSurface)},
		{"onTap", "(FF)V", reinterpret_cast<void*>(&NativeOnTap)},
		{"onClose", "()V", reinterpret_cast<void*>(&NativeOnClose)},
		{"onTorch", "()V", reinterpret_cast<void*>(&NativeOnTorch)},
	};

	if (env->RegisterNatives(bridge, bridgeMethods, 1) != JNI_OK
			|| env->RegisterNatives(overlay, overlayMethods, 4) != JNI_OK
			|| ClearPendingException(env, "RegisterNatives")) {
		LOGE("RegisterNatives failed");
		return false;
	}

	g_bridgePost = env->GetStaticMethodID(bridge, "post", "(Landroid/app/Activity;J)V");
	g_overlayShow = env->GetStaticMethodID(overlay, "show", "(Landroid/app/Activity;)V");
	g_overlayHide = env->GetStaticMethodID(overlay, "hide", "()V");
	g_overlayHasPermission = env->GetStaticMethodID(overlay,
		"hasCameraPermission", "(Landroid/app/Activity;)Z");
	g_overlayRequestPermission = env->GetStaticMethodID(overlay,
		"requestCameraPermission", "(Landroid/app/Activity;)V");
	g_overlayShowMarkers = env->GetStaticMethodID(overlay, "showMarkers", "([F)V");

	if (!g_bridgePost || !g_overlayShow || !g_overlayHide
			|| !g_overlayHasPermission || !g_overlayRequestPermission || !g_overlayShowMarkers
			|| ClearPendingException(env, "GetStaticMethodID")) {
		LOGE("Java part method lookup failed");
		return false;
	}

	g_bridgeCls = static_cast<jclass>(env->NewGlobalRef(bridge));
	g_overlayCls = static_cast<jclass>(env->NewGlobalRef(overlay));
	LOGI("Java part ready (UiBridge, ScannerOverlay)");
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

bool EnsureInfra(IAddInDefBase* connect)
{
	if (!connect)
		return false;

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

	return EnsureJavaPart(env, helper);
}

} // namespace

extern "C" JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void* /*reserved*/)
{
	g_vm = vm;
	LOGI("JNI_OnLoad: JavaVM captured");
	return JNI_VERSION_1_6;
}

namespace bsz::android {

StartScanResult StartScanning(IAddInDefBase* connect, BarcodeScannerAddIn* owner,
	const std::string& settingsJson)
{
	if (!EnsureInfra(connect))
		return StartScanResult::Failed;

	JNIEnv* env = Env();

	if (!env)
		return StartScanResult::Failed;

	{
		std::lock_guard<std::mutex> lock(g_ownerMutex);
		g_owner = owner;
	}

	g_autoClose = JsonFlag(settingsJson, "autoClose", true);
	g_torchOnStart = JsonFlag(settingsJson, "torch", false);

	const jboolean granted = env->CallStaticBooleanMethod(g_overlayCls,
		g_overlayHasPermission, g_activity);

	if (ClearPendingException(env, "hasCameraPermission"))
		return StartScanResult::Failed;

	if (!granted) {

		LOGI("CAMERA permission is not granted, requesting");
		RunOnUiThread(env, [] {

			JNIEnv* uiEnv = Env();

			if (!uiEnv)
				return;

			uiEnv->CallStaticVoidMethod(g_overlayCls, g_overlayRequestPermission, g_activity);
			ClearPendingException(uiEnv, "requestCameraPermission");

		});

		return StartScanResult::PermissionRequested;
	}

	const bool posted = RunOnUiThread(env, [] {

		JNIEnv* uiEnv = Env();

		if (!uiEnv)
			return;

		uiEnv->CallStaticVoidMethod(g_overlayCls, g_overlayShow, g_activity);
		ClearPendingException(uiEnv, "ScannerOverlay.show");

	});

	return posted ? StartScanResult::Started : StartScanResult::Failed;
}

bool StopScanning()
{
	{
		std::lock_guard<std::mutex> lock(g_ownerMutex);
		g_owner = nullptr;
	}

	if (!g_overlayCls || !g_activity)
		return true; // сканер не показывался

	JNIEnv* env = Env();

	if (!env)
		return false;

	// hide() снимет View, surfaceDestroyed -> onSurface(null) остановит камеру.
	return RunOnUiThread(env, [] {

		JNIEnv* uiEnv = Env();

		if (!uiEnv)
			return;

		uiEnv->CallStaticVoidMethod(g_overlayCls, g_overlayHide);
		ClearPendingException(uiEnv, "ScannerOverlay.hide");

	});
}

bool SetTorch(bool on)
{
	return CameraSetTorch(on);
}

} // namespace bsz::android

#endif // __ANDROID__
