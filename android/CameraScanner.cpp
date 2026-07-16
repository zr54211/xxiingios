#if defined(__ANDROID__)

#include "CameraScanner.h"

#include <android/api-level.h>
#include <android/log.h>
#include <android/native_window.h>
#include <camera/NdkCameraCaptureSession.h>
#include <camera/NdkCameraDevice.h>
#include <camera/NdkCameraManager.h>
#include <camera/NdkCameraMetadata.h>
#include <camera/NdkCaptureRequest.h>
#include <dlfcn.h>
#include <media/NdkImage.h>
#include <media/NdkImageReader.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <vector>

#include "Decoder.h"

#define LOG_TAG "BarcodeScannerZXing"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace {

// Camera2 NDK доступен с API 24, а компонента собирается под android-21,
// поэтому библиотеки загружаются динамически, без линковки.
struct CameraApi {
	void* cam = nullptr;
	void* media = nullptr;

	decltype(&ACameraManager_create) ManagerCreate{};
	decltype(&ACameraManager_delete) ManagerDelete{};
	decltype(&ACameraManager_getCameraIdList) GetCameraIdList{};
	decltype(&ACameraManager_deleteCameraIdList) DeleteCameraIdList{};
	decltype(&ACameraManager_getCameraCharacteristics) GetCharacteristics{};
	decltype(&ACameraMetadata_getConstEntry) MetadataGetConstEntry{};
	decltype(&ACameraMetadata_free) MetadataFree{};
	decltype(&ACameraManager_openCamera) OpenCamera{};
	decltype(&ACameraDevice_close) DeviceClose{};
	decltype(&ACameraDevice_createCaptureRequest) CreateCaptureRequest{};
	decltype(&ACameraDevice_createCaptureSession) CreateCaptureSession{};
	decltype(&ACaptureRequest_free) RequestFree{};
	decltype(&ACaptureRequest_addTarget) RequestAddTarget{};
	decltype(&ACaptureRequest_setEntry_u8) RequestSetEntryU8{};
	decltype(&ACameraOutputTarget_create) OutputTargetCreate{};
	decltype(&ACameraOutputTarget_free) OutputTargetFree{};
	decltype(&ACaptureSessionOutput_create) SessionOutputCreate{};
	decltype(&ACaptureSessionOutput_free) SessionOutputFree{};
	decltype(&ACaptureSessionOutputContainer_create) OutputContainerCreate{};
	decltype(&ACaptureSessionOutputContainer_add) OutputContainerAdd{};
	decltype(&ACaptureSessionOutputContainer_free) OutputContainerFree{};
	decltype(&ACameraCaptureSession_setRepeatingRequest) SetRepeatingRequest{};
	decltype(&ACameraCaptureSession_stopRepeating) StopRepeating{};
	decltype(&ACameraCaptureSession_close) SessionClose{};

	decltype(&AImageReader_new) ImageReaderNew{};
	decltype(&AImageReader_delete) ImageReaderDelete{};
	decltype(&AImageReader_getWindow) ImageReaderGetWindow{};
	decltype(&AImageReader_setImageListener) ImageReaderSetListener{};
	decltype(&AImageReader_acquireLatestImage) AcquireLatestImage{};
	decltype(&AImage_delete) ImageDelete{};
	decltype(&AImage_getWidth) ImageGetWidth{};
	decltype(&AImage_getHeight) ImageGetHeight{};
	decltype(&AImage_getPlaneRowStride) ImageGetPlaneRowStride{};
	decltype(&AImage_getPlaneData) ImageGetPlaneData{};

	template <typename Fn>
	bool Sym(void* lib, const char* name, Fn& out)
	{
		out = reinterpret_cast<Fn>(dlsym(lib, name));

		if (!out) {
			LOGE("dlsym(%s) failed", name);
			return false;
		}

		return true;
	}

	bool Load()
	{
		if (cam)
			return true;

		if (android_get_device_api_level() < 24) {
			LOGE("Camera2 NDK requires API 24+, device has %d", android_get_device_api_level());
			return false;
		}

		cam = dlopen("libcamera2ndk.so", RTLD_NOW);
		media = dlopen("libmediandk.so", RTLD_NOW);

		if (!cam || !media) {
			LOGE("dlopen camera2ndk/mediandk failed: %s", dlerror());
			return false;
		}

		return Sym(cam, "ACameraManager_create", ManagerCreate)
			&& Sym(cam, "ACameraManager_delete", ManagerDelete)
			&& Sym(cam, "ACameraManager_getCameraIdList", GetCameraIdList)
			&& Sym(cam, "ACameraManager_deleteCameraIdList", DeleteCameraIdList)
			&& Sym(cam, "ACameraManager_getCameraCharacteristics", GetCharacteristics)
			&& Sym(cam, "ACameraMetadata_getConstEntry", MetadataGetConstEntry)
			&& Sym(cam, "ACameraMetadata_free", MetadataFree)
			&& Sym(cam, "ACameraManager_openCamera", OpenCamera)
			&& Sym(cam, "ACameraDevice_close", DeviceClose)
			&& Sym(cam, "ACameraDevice_createCaptureRequest", CreateCaptureRequest)
			&& Sym(cam, "ACameraDevice_createCaptureSession", CreateCaptureSession)
			&& Sym(cam, "ACaptureRequest_free", RequestFree)
			&& Sym(cam, "ACaptureRequest_addTarget", RequestAddTarget)
			&& Sym(cam, "ACaptureRequest_setEntry_u8", RequestSetEntryU8)
			&& Sym(cam, "ACameraOutputTarget_create", OutputTargetCreate)
			&& Sym(cam, "ACameraOutputTarget_free", OutputTargetFree)
			&& Sym(cam, "ACaptureSessionOutput_create", SessionOutputCreate)
			&& Sym(cam, "ACaptureSessionOutput_free", SessionOutputFree)
			&& Sym(cam, "ACaptureSessionOutputContainer_create", OutputContainerCreate)
			&& Sym(cam, "ACaptureSessionOutputContainer_add", OutputContainerAdd)
			&& Sym(cam, "ACaptureSessionOutputContainer_free", OutputContainerFree)
			&& Sym(cam, "ACameraCaptureSession_setRepeatingRequest", SetRepeatingRequest)
			&& Sym(cam, "ACameraCaptureSession_stopRepeating", StopRepeating)
			&& Sym(cam, "ACameraCaptureSession_close", SessionClose)
			&& Sym(media, "AImageReader_new", ImageReaderNew)
			&& Sym(media, "AImageReader_delete", ImageReaderDelete)
			&& Sym(media, "AImageReader_getWindow", ImageReaderGetWindow)
			&& Sym(media, "AImageReader_setImageListener", ImageReaderSetListener)
			&& Sym(media, "AImageReader_acquireLatestImage", AcquireLatestImage)
			&& Sym(media, "AImage_delete", ImageDelete)
			&& Sym(media, "AImage_getWidth", ImageGetWidth)
			&& Sym(media, "AImage_getHeight", ImageGetHeight)
			&& Sym(media, "AImage_getPlaneRowStride", ImageGetPlaneRowStride)
			&& Sym(media, "AImage_getPlaneData", ImageGetPlaneData);
	}
};

CameraApi g_api;

struct CameraState {
	ACameraManager* manager = nullptr;
	ACameraDevice* device = nullptr;
	AImageReader* reader = nullptr;
	ACaptureSessionOutputContainer* outputs = nullptr;
	ACaptureSessionOutput* outPreview = nullptr;
	ACaptureSessionOutput* outReader = nullptr;
	ACameraOutputTarget* tgtPreview = nullptr;
	ACameraOutputTarget* tgtReader = nullptr;
	ACaptureRequest* request = nullptr;
	ACameraCaptureSession* session = nullptr;
};

std::mutex g_mutex;
CameraState g_cam;
bsz::android::ScanEmitFn g_emit;
std::atomic<bool> g_running{false};

// Подавление повторной отправки того же результата при непрерывном сканировании.
std::string g_lastJson;
std::chrono::steady_clock::time_point g_lastEmitAt;

void OnDeviceDisconnected(void* /*ctx*/, ACameraDevice* /*device*/)
{
	LOGE("Camera disconnected");
}

void OnDeviceError(void* /*ctx*/, ACameraDevice* /*device*/, int error)
{
	LOGE("Camera device error %d", error);
}

void OnSessionClosed(void* /*ctx*/, ACameraCaptureSession* /*session*/) {}
void OnSessionReady(void* /*ctx*/, ACameraCaptureSession* /*session*/) {}
void OnSessionActive(void* /*ctx*/, ACameraCaptureSession* /*session*/) {}

void OnImageAvailable(void* /*ctx*/, AImageReader* reader)
{
	if (!g_running.load())
		return;

	// Не блокируемся: если идёт остановка камеры — просто пропускаем кадр.
	std::unique_lock<std::mutex> lock(g_mutex, std::try_to_lock);

	if (!lock.owns_lock() || !g_running.load())
		return;

	AImage* image = nullptr;

	if (g_api.AcquireLatestImage(reader, &image) != AMEDIA_OK || !image)
		return;

	int32_t width = 0;
	int32_t height = 0;
	int32_t rowStride = 0;
	uint8_t* yPlane = nullptr;
	int yLen = 0;

	g_api.ImageGetWidth(image, &width);
	g_api.ImageGetHeight(image, &height);
	g_api.ImageGetPlaneRowStride(image, 0, &rowStride);
	g_api.ImageGetPlaneData(image, 0, &yPlane, &yLen);

	if (!yPlane || width <= 0 || height <= 0) {
		g_api.ImageDelete(image);
		return;
	}

	static std::vector<uint8_t> buffer;
	buffer.resize(static_cast<size_t>(width) * height);

	if (rowStride == width) {
		std::memcpy(buffer.data(), yPlane, buffer.size());
	} else {

		for (int32_t row = 0; row < height; ++row)
			std::memcpy(buffer.data() + static_cast<size_t>(row) * width,
				yPlane + static_cast<size_t>(row) * rowStride, width);

	}

	g_api.ImageDelete(image);

	const std::string json = bsz::DecodeLuminance(buffer.data(), width, height);

	if (json.rfind(R"({"found":0)", 0) == 0)
		return;

	// Один и тот же код не чаще раза в 2 секунды.
	const auto now = std::chrono::steady_clock::now();

	if (json == g_lastJson && now - g_lastEmitAt < std::chrono::seconds(2))
		return;

	g_lastJson = json;
	g_lastEmitAt = now;
	LOGI("Barcode found: %s", json.c_str());

	if (g_emit)
		g_emit(json);
}

// Выбирает размер YUV-потока анализа: максимальная площадь, не превышающая
// targetW x targetH (плотные QR требуют разрешения повыше, но без фанатизма —
// каждый кадр прогоняется через zxing целиком).
bool PickAnalysisSize(const char* cameraId, int targetW, int targetH, int& outW, int& outH)
{
	ACameraMetadata* meta = nullptr;

	if (g_api.GetCharacteristics(g_cam.manager, cameraId, &meta) != ACAMERA_OK)
		return false;

	ACameraMetadata_const_entry entry{};
	const bool ok = g_api.MetadataGetConstEntry(meta,
		ACAMERA_SCALER_AVAILABLE_STREAM_CONFIGURATIONS, &entry) == ACAMERA_OK;

	int64_t bestArea = 0;
	const int64_t maxArea = static_cast<int64_t>(targetW) * targetH;

	if (ok) {

		// Записи по 4 значения: format, width, height, isInput.
		for (uint32_t i = 0; i + 3 < entry.count; i += 4) {

			if (entry.data.i32[i] != AIMAGE_FORMAT_YUV_420_888 || entry.data.i32[i + 3] != 0)
				continue;

			const int w = entry.data.i32[i + 1];
			const int h = entry.data.i32[i + 2];
			const int64_t area = static_cast<int64_t>(w) * h;

			if (area > bestArea && area <= maxArea) {
				bestArea = area;
				outW = w;
				outH = h;
			}

		}

	}

	g_api.MetadataFree(meta);
	return bestArea > 0;
}

// Выбирает заднюю камеру; при неудаче возвращает пустую строку.
std::string PickBackCamera()
{
	ACameraIdList* ids = nullptr;

	if (g_api.GetCameraIdList(g_cam.manager, &ids) != ACAMERA_OK || !ids)
		return {};

	std::string result;

	for (int i = 0; i < ids->numCameras; ++i) {

		ACameraMetadata* meta = nullptr;

		if (g_api.GetCharacteristics(g_cam.manager, ids->cameraIds[i], &meta) != ACAMERA_OK)
			continue;

		ACameraMetadata_const_entry entry{};
		const bool isBack =
			g_api.MetadataGetConstEntry(meta, ACAMERA_LENS_FACING, &entry) == ACAMERA_OK
			&& entry.count > 0 && entry.data.u8[0] == ACAMERA_LENS_FACING_BACK;
		g_api.MetadataFree(meta);

		if (isBack) {
			result = ids->cameraIds[i];
			break;
		}

	}

	if (result.empty() && ids->numCameras > 0)
		result = ids->cameraIds[0];

	g_api.DeleteCameraIdList(ids);
	return result;
}

void TeardownLocked()
{
	if (g_cam.session) {
		g_api.StopRepeating(g_cam.session);
		g_api.SessionClose(g_cam.session);
		g_cam.session = nullptr;
	}

	if (g_cam.request) {
		g_api.RequestFree(g_cam.request);
		g_cam.request = nullptr;
	}

	if (g_cam.tgtPreview) {
		g_api.OutputTargetFree(g_cam.tgtPreview);
		g_cam.tgtPreview = nullptr;
	}

	if (g_cam.tgtReader) {
		g_api.OutputTargetFree(g_cam.tgtReader);
		g_cam.tgtReader = nullptr;
	}

	if (g_cam.outputs) {
		g_api.OutputContainerFree(g_cam.outputs);
		g_cam.outputs = nullptr;
		g_cam.outPreview = nullptr;
		g_cam.outReader = nullptr;
	}

	if (g_cam.device) {
		g_api.DeviceClose(g_cam.device);
		g_cam.device = nullptr;
	}

	if (g_cam.reader) {
		g_api.ImageReaderDelete(g_cam.reader);
		g_cam.reader = nullptr;
	}

	if (g_cam.manager) {
		g_api.ManagerDelete(g_cam.manager);
		g_cam.manager = nullptr;
	}
}

} // namespace

namespace bsz::android {

bool CameraStart(ANativeWindow* previewWindow, int width, int height, ScanEmitFn emit)
{
	std::lock_guard<std::mutex> lock(g_mutex);

	if (g_running.load()) {
		LOGI("CameraStart: already running");
		return true;
	}

	if (!previewWindow || !g_api.Load())
		return false;

	g_emit = std::move(emit);
	g_lastJson.clear();

	g_cam.manager = g_api.ManagerCreate();

	if (!g_cam.manager)
		return false;

	const std::string cameraId = PickBackCamera();

	if (cameraId.empty()) {
		LOGE("No cameras available");
		TeardownLocked();
		return false;
	}

	// width/height трактуются как максимум потока анализа; берём лучший
	// поддерживаемый размер камеры, не превышающий его.
	int analysisW = 1280;
	int analysisH = 720;

	if (!PickAnalysisSize(cameraId.c_str(), width, height, analysisW, analysisH))
		LOGE("PickAnalysisSize failed, falling back to %dx%d", analysisW, analysisH);

	static ACameraDevice_StateCallbacks deviceCallbacks = {
		nullptr, OnDeviceDisconnected, OnDeviceError};

	if (g_api.OpenCamera(g_cam.manager, cameraId.c_str(), &deviceCallbacks, &g_cam.device)
			!= ACAMERA_OK || !g_cam.device) {
		LOGE("ACameraManager_openCamera failed");
		TeardownLocked();
		return false;
	}

	if (g_api.ImageReaderNew(analysisW, analysisH, AIMAGE_FORMAT_YUV_420_888, 4, &g_cam.reader)
			!= AMEDIA_OK || !g_cam.reader) {
		LOGE("AImageReader_new(%dx%d) failed", analysisW, analysisH);
		TeardownLocked();
		return false;
	}

	ANativeWindow* readerWindow = nullptr;
	g_api.ImageReaderGetWindow(g_cam.reader, &readerWindow);

	AImageReader_ImageListener listener = {nullptr, OnImageAvailable};
	g_api.ImageReaderSetListener(g_cam.reader, &listener);

	g_api.OutputContainerCreate(&g_cam.outputs);
	g_api.SessionOutputCreate(previewWindow, &g_cam.outPreview);
	g_api.SessionOutputCreate(readerWindow, &g_cam.outReader);
	g_api.OutputContainerAdd(g_cam.outputs, g_cam.outPreview);
	g_api.OutputContainerAdd(g_cam.outputs, g_cam.outReader);

	static ACameraCaptureSession_stateCallbacks sessionCallbacks = {
		nullptr, OnSessionClosed, OnSessionReady, OnSessionActive};

	if (g_api.CreateCaptureSession(g_cam.device, g_cam.outputs, &sessionCallbacks, &g_cam.session)
			!= ACAMERA_OK) {
		LOGE("createCaptureSession failed (unsupported output sizes?)");
		TeardownLocked();
		return false;
	}

	if (g_api.CreateCaptureRequest(g_cam.device, TEMPLATE_PREVIEW, &g_cam.request) != ACAMERA_OK) {
		LOGE("createCaptureRequest failed");
		TeardownLocked();
		return false;
	}

	g_api.OutputTargetCreate(previewWindow, &g_cam.tgtPreview);
	g_api.OutputTargetCreate(readerWindow, &g_cam.tgtReader);
	g_api.RequestAddTarget(g_cam.request, g_cam.tgtPreview);
	g_api.RequestAddTarget(g_cam.request, g_cam.tgtReader);

	const uint8_t afMode = ACAMERA_CONTROL_AF_MODE_CONTINUOUS_PICTURE;
	g_api.RequestSetEntryU8(g_cam.request, ACAMERA_CONTROL_AF_MODE, 1, &afMode);

	if (g_api.SetRepeatingRequest(g_cam.session, nullptr, 1, &g_cam.request, nullptr)
			!= ACAMERA_OK) {
		LOGE("setRepeatingRequest failed");
		TeardownLocked();
		return false;
	}

	g_running.store(true);
	LOGI("Camera started (analysis %dx%d)", analysisW, analysisH);
	return true;
}

void CameraStop()
{
	g_running.store(false);

	std::lock_guard<std::mutex> lock(g_mutex);
	TeardownLocked();
	g_emit = nullptr;
	LOGI("Camera stopped");
}

} // namespace bsz::android

#endif // __ANDROID__
