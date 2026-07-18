// Экран сканера iOS: AVFoundation-камера, превью с letterbox, уголки-рамка
// (паритет с android/ScannerOverlay.java), фонарик, tap-to-focus, autoClose.
// Кадры: AVCaptureVideoDataOutput 1080p, Y-плоскость → bsz::DecodeLuminanceEx.

#include "IosScanner.h"

#import <AVFoundation/AVFoundation.h>
#import <UIKit/UIKit.h>

#include <array>
#include <atomic>
#include <chrono>
#include <string>
#include <vector>

#include "BarcodeScannerAddIn.h"
#include "Decoder.h"

@class BSZScanner;

namespace {

// Компонента может умереть, пока экран открыт, — владелец атомарный,
// обнуляется синхронно из StopScanning.
std::atomic<BarcodeScannerAddIn*> g_owner{nullptr};

// Только главный поток.
BSZScanner* g_scanner = nil;

bool g_autoClose = true;
bool g_torchOnStart = false;

// Простейший разбор флага в настройках вида {"torch":false,"autoClose":true}.
bool JsonFlag(const std::string& json, const char* name, bool defaultValue)
{
	const std::string key = std::string("\"") + name + "\"";
	size_t pos = json.find(key);

	if (pos == std::string::npos)
		return defaultValue;

	pos = json.find(':', pos + key.size());

	if (pos == std::string::npos)
		return defaultValue;

	while (++pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) {
	}

	if (json.compare(pos, 4, "true") == 0)
		return true;

	if (json.compare(pos, 5, "false") == 0)
		return false;

	return defaultValue;
}

void RunOnMain(void (^block)(void))
{
	if (NSThread.isMainThread)
		block();
	else
		dispatch_async(dispatch_get_main_queue(), block);
}

UIWindow* KeyWindow()
{
	UIApplication* app = UIApplication.sharedApplication;

	if (@available(iOS 13.0, *)) {

		for (UIScene* scene in app.connectedScenes) {

			if (![scene isKindOfClass:[UIWindowScene class]])
				continue;

			for (UIWindow* window in ((UIWindowScene*)scene).windows)

				if (window.isKeyWindow)
					return window;

		}

	}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
	if (app.keyWindow)
		return app.keyWindow;

	return app.windows.firstObject;
#pragma clang diagnostic pop
}

void CloseScanner(bool emitCancelled);

} // namespace

// Слой уголков: в покое — широкая рамка наводки, при находке жёлтые уголки
// перетекают к углам кода (зеркало MarkerView из ScannerOverlay.java).
@interface BSZMarkerView : UIView
- (void)setMarkers:(const float*)normalizedPoints; // 8 float или NULL
@end

@implementation BSZMarkerView {
	CAShapeLayer* _shape;
	CADisplayLink* _link;
	float _target[8];
	float _current[8];
	BOOL _hasCode;
	BOOL _hasCurrent;
}

static const float kBracketPart = 0.10f;
static const float kLerp = 0.35f;
static const float kCodePaddingPx = 15.0f;

- (instancetype)initWithFrame:(CGRect)frame
{
	self = [super initWithFrame:frame];
	self.userInteractionEnabled = NO;
	self.backgroundColor = UIColor.clearColor;

	_shape = [CAShapeLayer layer];
	_shape.fillColor = UIColor.clearColor.CGColor;
	_shape.lineWidth = 4.0;
	_shape.lineCap = kCALineCapRound;
	_shape.lineJoin = kCALineJoinRound;
	[self.layer addSublayer:_shape];

	[self idleFrameTo:_target];
	_hasCurrent = NO;

	_link = [CADisplayLink displayLinkWithTarget:self selector:@selector(tick)];
	[_link addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes];
	return self;
}

- (void)invalidate
{
	[_link invalidate];
	_link = nil;
}

- (void)idleFrameTo:(float*)out
{
	const float w = (float)self.bounds.size.width;
	const float h = (float)self.bounds.size.height;

	if (w <= 0 || h <= 0)
		return;

	const float half = MIN(w, h) * 0.455f;
	const float left = (w / 2 - half) / w;
	const float right = (w / 2 + half) / w;
	const float top = (h / 2 - half) / h;
	const float bottom = (h / 2 + half) / h;
	const float frame[8] = {left, top, right, top, right, bottom, left, bottom};
	memcpy(out, frame, sizeof(frame));
}

// Углы кода приходят в его собственной системе координат и «крутятся» вместе
// с ним — сортировка по atan2 вокруг центра ведёт уголки к ближайшим углам.
- (void)sortToViewOrder:(float*)p
{
	float cx = 0;
	float cy = 0;

	for (int i = 0; i < 4; ++i) {
		cx += p[i * 2] / 4;
		cy += p[i * 2 + 1] / 4;
	}

	int order[4] = {0, 1, 2, 3};
	double angle[4];

	for (int i = 0; i < 4; ++i)
		angle[i] = atan2(p[i * 2 + 1] - cy, p[i * 2] - cx);

	for (int i = 0; i < 3; ++i) {

		for (int j = i + 1; j < 4; ++j) {

			if (angle[order[j]] < angle[order[i]]) {
				const int t = order[i];
				order[i] = order[j];
				order[j] = t;
			}

		}

	}

	float sorted[8];

	for (int i = 0; i < 4; ++i) {
		sorted[i * 2] = p[order[i] * 2];
		sorted[i * 2 + 1] = p[order[i] * 2 + 1];
	}

	memcpy(p, sorted, sizeof(sorted));
}

// Раздвигает четырёхугольник кода наружу от центра, рамка чуть шире кода.
- (void)expandAroundCode:(float*)p
{
	const float w = (float)self.bounds.size.width;
	const float h = (float)self.bounds.size.height;

	if (w <= 0 || h <= 0)
		return;

	float cx = 0;
	float cy = 0;

	for (int i = 0; i < 4; ++i) {
		cx += p[i * 2] * w / 4;
		cy += p[i * 2 + 1] * h / 4;
	}

	for (int i = 0; i < 4; ++i) {
		float px = p[i * 2] * w;
		float py = p[i * 2 + 1] * h;
		const float dx = px - cx;
		const float dy = py - cy;
		const float len = sqrtf(dx * dx + dy * dy);

		if (len > 1) {
			px += dx / len * kCodePaddingPx;
			py += dy / len * kCodePaddingPx;
		}

		p[i * 2] = px / w;
		p[i * 2 + 1] = py / h;
	}
}

- (void)setMarkers:(const float*)normalizedPoints
{
	_hasCode = normalizedPoints != NULL;

	if (_hasCode) {
		float p[8];
		memcpy(p, normalizedPoints, sizeof(p));
		[self sortToViewOrder:p];
		[self expandAroundCode:p];
		memcpy(_target, p, sizeof(p));
	} else {
		[self idleFrameTo:_target];
	}
}

- (void)tick
{
	if (!_hasCurrent) {
		memcpy(_current, _target, sizeof(_current));
		_hasCurrent = YES;
	}

	for (int i = 0; i < 8; ++i)
		_current[i] += (_target[i] - _current[i]) * kLerp;

	const float w = (float)self.bounds.size.width;
	const float h = (float)self.bounds.size.height;

	if (w <= 0 || h <= 0)
		return;

	UIBezierPath* path = [UIBezierPath bezierPath];

	for (int i = 0; i < 4; ++i) {
		const float cx = _current[i * 2] * w;
		const float cy = _current[i * 2 + 1] * h;
		const float px = _current[((i + 3) % 4) * 2] * w;
		const float py = _current[((i + 3) % 4) * 2 + 1] * h;
		const float nx = _current[((i + 1) % 4) * 2] * w;
		const float ny = _current[((i + 1) % 4) * 2 + 1] * h;

		[path moveToPoint:CGPointMake(cx + (px - cx) * kBracketPart, cy + (py - cy) * kBracketPart)];
		[path addLineToPoint:CGPointMake(cx, cy)];
		[path addLineToPoint:CGPointMake(cx + (nx - cx) * kBracketPart, cy + (ny - cy) * kBracketPart)];
	}

	[CATransaction begin];
	[CATransaction setDisableActions:YES];
	_shape.path = path.CGPath;
	_shape.strokeColor = (_hasCode
		? [UIColor colorWithRed:1.0 green:0.839 blue:0.0 alpha:1.0]        // FFD600
		: [UIColor colorWithWhite:1.0 alpha:0.8]).CGColor;                 // CCFFFFFF
	[CATransaction commit];
}

@end

// Экран сканера: сессия камеры, превью, уголки, кнопки, декодирование кадров.
@interface BSZScanner : NSObject <AVCaptureVideoDataOutputSampleBufferDelegate>
@property(nonatomic, strong) UIView* root;
- (BOOL)showAndStart;
- (void)stop;
- (BOOL)setTorch:(BOOL)on;
@end

@implementation BSZScanner {
	AVCaptureSession* _session;
	AVCaptureDevice* _device;
	AVCaptureVideoPreviewLayer* _previewLayer;
	BSZMarkerView* _markers;
	dispatch_queue_t _sessionQueue;
	dispatch_queue_t _videoQueue;
	std::vector<uint8_t> _lumBuffer;
	std::string _lastText;
	std::chrono::steady_clock::time_point _lastEmitAt;
	BOOL _markersShown;
	BOOL _closing;
}

- (instancetype)init
{
	self = [super init];
	_sessionQueue = dispatch_queue_create("bsz.session", DISPATCH_QUEUE_SERIAL);
	_videoQueue = dispatch_queue_create("bsz.video", DISPATCH_QUEUE_SERIAL);
	return self;
}

- (BOOL)showAndStart
{
	UIWindow* window = KeyWindow();

	if (!window) {
		NSLog(@"BarcodeScannerZXing: key window not found");
		return NO;
	}

	_device = [AVCaptureDevice defaultDeviceWithMediaType:AVMediaTypeVideo];

	if (!_device) {
		NSLog(@"BarcodeScannerZXing: no camera device");
		return NO;
	}

	_session = [[AVCaptureSession alloc] init];

	if ([_session canSetSessionPreset:AVCaptureSessionPreset1920x1080])
		_session.sessionPreset = AVCaptureSessionPreset1920x1080;

	NSError* error = nil;
	AVCaptureDeviceInput* input = [AVCaptureDeviceInput deviceInputWithDevice:_device error:&error];

	if (!input || ![_session canAddInput:input]) {
		NSLog(@"BarcodeScannerZXing: camera input failed: %@", error);
		return NO;
	}

	[_session addInput:input];

	AVCaptureVideoDataOutput* output = [[AVCaptureVideoDataOutput alloc] init];
	output.videoSettings = @{
		(id)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_420YpCbCr8BiPlanarFullRange)
	};
	output.alwaysDiscardsLateVideoFrames = YES;
	[output setSampleBufferDelegate:self queue:_videoQueue];

	if (![_session canAddOutput:output]) {
		NSLog(@"BarcodeScannerZXing: cannot add video output");
		return NO;
	}

	[_session addOutput:output];

	// Портретная ориентация кадров: превью и анализ в одной системе координат.
	AVCaptureConnection* connection = [output connectionWithMediaType:AVMediaTypeVideo];

	if (connection.isVideoOrientationSupported)
		connection.videoOrientation = AVCaptureVideoOrientationPortrait;

	[self buildOverlayIn:window];

	AVCaptureDevice* device = _device;
	dispatch_async(_sessionQueue, ^{
		[self->_session startRunning];

		// Фокус настраивается после старта сессии (до старта настройки
		// может сбросить первый запуск). Режим сканера: непрерывный AF по
		// центру, ограничение ближней зоной, без «плавной» фокусировки.
		if ([device lockForConfiguration:nil]) {

			if (device.isFocusPointOfInterestSupported)
				device.focusPointOfInterest = CGPointMake(0.5, 0.5);

			if ([device isFocusModeSupported:AVCaptureFocusModeContinuousAutoFocus])
				device.focusMode = AVCaptureFocusModeContinuousAutoFocus;

			if (device.isAutoFocusRangeRestrictionSupported)
				device.autoFocusRangeRestriction = AVCaptureAutoFocusRangeRestrictionNear;

			if (device.isSmoothAutoFocusSupported)
				device.smoothAutoFocusEnabled = NO;

			[device unlockForConfiguration];
		}
	});

	if (g_torchOnStart)
		[self setTorch:YES];

	NSLog(@"BarcodeScannerZXing: scanner started");
	return YES;
}

- (void)buildOverlayIn:(UIWindow*)window
{
	UIView* root = [[UIView alloc] initWithFrame:window.bounds];
	root.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
	root.backgroundColor = UIColor.blackColor;

	// Превью с пропорциями портретного кадра (9:16), letterbox по центру.
	const CGFloat w = window.bounds.size.width;
	const CGFloat h = window.bounds.size.height;
	CGFloat viewWidth = w;
	CGFloat viewHeight = viewWidth * 16.0 / 9.0;

	if (viewHeight > h) {
		viewHeight = h;
		viewWidth = viewHeight * 9.0 / 16.0;
	}

	const CGRect videoFrame = CGRectMake((w - viewWidth) / 2, (h - viewHeight) / 2,
		viewWidth, viewHeight);

	_previewLayer = [AVCaptureVideoPreviewLayer layerWithSession:_session];
	_previewLayer.videoGravity = AVLayerVideoGravityResize;
	_previewLayer.frame = videoFrame;
	[root.layer addSublayer:_previewLayer];

	_markers = [[BSZMarkerView alloc] initWithFrame:videoFrame];
	[root addSubview:_markers];

	// Тап по превью — фокусировка в точке.
	UITapGestureRecognizer* tap =
		[[UITapGestureRecognizer alloc] initWithTarget:self action:@selector(onTap:)];
	[root addGestureRecognizer:tap];

	// Кнопка закрытия (справа сверху) и фонарик (слева сверху).
	UIButton* close = [self overlayButtonWithTitle:@"✕"];
	[close addTarget:self action:@selector(onClose) forControlEvents:UIControlEventTouchUpInside];
	UIButton* torch = [self overlayButtonWithTitle:@"🔦"];
	[torch addTarget:self action:@selector(onTorch) forControlEvents:UIControlEventTouchUpInside];

	const CGFloat topInset = window.safeAreaInsets.top + 12;
	close.frame = CGRectMake(w - 16 - 48, topInset, 48, 48);
	close.autoresizingMask = UIViewAutoresizingFlexibleLeftMargin | UIViewAutoresizingFlexibleBottomMargin;
	torch.frame = CGRectMake(16, topInset, 48, 48);
	torch.autoresizingMask = UIViewAutoresizingFlexibleRightMargin | UIViewAutoresizingFlexibleBottomMargin;

	[root addSubview:close];
	[root addSubview:torch];
	[window addSubview:root];

	self.root = root;
}

- (UIButton*)overlayButtonWithTitle:(NSString*)title
{
	UIButton* button = [UIButton buttonWithType:UIButtonTypeSystem];
	[button setTitle:title forState:UIControlStateNormal];
	[button setTitleColor:UIColor.whiteColor forState:UIControlStateNormal];
	button.titleLabel.font = [UIFont systemFontOfSize:22.0];
	button.backgroundColor = [UIColor colorWithWhite:0.0 alpha:0.4];
	button.layer.cornerRadius = 24.0;
	return button;
}

- (void)onClose
{
	CloseScanner(true);
}

- (void)onTorch
{
	const BOOL on = !(_device.hasTorch && _device.torchMode == AVCaptureTorchModeOn);
	[self setTorch:on];
}

- (void)onTap:(UITapGestureRecognizer*)gesture
{
	const CGPoint layerPoint = [gesture locationInView:self.root];
	const CGPoint devicePoint =
		[_previewLayer captureDevicePointOfInterestForPoint:
			[self.root.layer convertPoint:layerPoint toLayer:_previewLayer]];

	if (![_device lockForConfiguration:nil])
		return;

	if (_device.isFocusPointOfInterestSupported
		&& [_device isFocusModeSupported:AVCaptureFocusModeAutoFocus]) {
		_device.focusPointOfInterest = devicePoint;
		_device.focusMode = AVCaptureFocusModeAutoFocus;
	}

	if (_device.isExposurePointOfInterestSupported
		&& [_device isExposureModeSupported:AVCaptureExposureModeContinuousAutoExposure]) {
		_device.exposurePointOfInterest = devicePoint;
		_device.exposureMode = AVCaptureExposureModeContinuousAutoExposure;
	}

	[_device unlockForConfiguration];

	// Через 4 секунды — обратно в непрерывный автофокус (как на Android).
	__weak BSZScanner* weakSelf = self;
	dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(4.0 * NSEC_PER_SEC)),
		dispatch_get_main_queue(), ^{
			BSZScanner* strongSelf = weakSelf;

			if (!strongSelf)
				return;

			if ([strongSelf->_device lockForConfiguration:nil]) {

				if ([strongSelf->_device isFocusModeSupported:AVCaptureFocusModeContinuousAutoFocus])
					strongSelf->_device.focusMode = AVCaptureFocusModeContinuousAutoFocus;

				[strongSelf->_device unlockForConfiguration];
			}
		});
}

- (BOOL)setTorch:(BOOL)on
{
	if (!_device.hasTorch || ![_device lockForConfiguration:nil])
		return NO;

	BOOL result = YES;

	if (on && [_device isTorchModeSupported:AVCaptureTorchModeOn])
		_device.torchMode = AVCaptureTorchModeOn;
	else if (!on && [_device isTorchModeSupported:AVCaptureTorchModeOff])
		_device.torchMode = AVCaptureTorchModeOff;
	else
		result = NO;

	[_device unlockForConfiguration];
	return result;
}

- (void)stop
{
	_closing = YES;
	[_markers invalidate];

	AVCaptureSession* session = _session;
	dispatch_async(_sessionQueue, ^{
		[session stopRunning];
	});

	[self.root removeFromSuperview];
	self.root = nil;
	_session = nil;
	_device = nil;
}

// Поток _videoQueue: копия Y-плоскости и распознавание.
- (void)captureOutput:(AVCaptureOutput*)output
	didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
	fromConnection:(AVCaptureConnection*)connection
{
	if (_closing)
		return;

	CVImageBufferRef pixelBuffer = CMSampleBufferGetImageBuffer(sampleBuffer);

	if (!pixelBuffer)
		return;

	if (CVPixelBufferLockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly) != kCVReturnSuccess)
		return;

	const int width = (int)CVPixelBufferGetWidthOfPlane(pixelBuffer, 0);
	const int height = (int)CVPixelBufferGetHeightOfPlane(pixelBuffer, 0);
	const size_t stride = CVPixelBufferGetBytesPerRowOfPlane(pixelBuffer, 0);
	const uint8_t* base = (const uint8_t*)CVPixelBufferGetBaseAddressOfPlane(pixelBuffer, 0);

	if (base && width > 0 && height > 0) {
		_lumBuffer.resize((size_t)width * height);

		for (int y = 0; y < height; ++y)
			memcpy(_lumBuffer.data() + (size_t)y * width, base + y * stride, width);
	}

	CVPixelBufferUnlockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);

	if (_lumBuffer.empty())
		return;

	const bsz::DecodeResult decoded = bsz::DecodeLuminanceEx(_lumBuffer.data(), width, height);

	// Сопровождение рамкой: обновляется каждым кадром, гаснет без кода.
	const bool found = decoded.found > 0;

	if (found || _markersShown) {
		_markersShown = found;
		std::array<float, 8> points{};

		if (found)
			memcpy(points.data(), decoded.points, sizeof(decoded.points));

		BSZMarkerView* markers = _markers;
		dispatch_async(dispatch_get_main_queue(), ^{
			[markers setMarkers:(found ? points.data() : NULL)];
		});
	}

	if (!found)
		return;

	// Один и тот же код — не чаще раза в 2 секунды.
	const auto now = std::chrono::steady_clock::now();

	if (decoded.firstText == _lastText && now - _lastEmitAt < std::chrono::seconds(2))
		return;

	_lastText = decoded.firstText;
	_lastEmitAt = now;
	NSLog(@"BarcodeScannerZXing: barcode found");

	if (BarcodeScannerAddIn* owner = g_owner.load())
		owner->EmitScanResult(decoded.json);

	if (g_autoClose) {
		_closing = YES;

		// Дать рамке мелькнуть у кода перед закрытием экрана.
		dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.4 * NSEC_PER_SEC)),
			dispatch_get_main_queue(), ^{
				CloseScanner(false);
			});
	}
}

@end

namespace {

// Главный поток. Закрывает экран; emitCancelled — пользовательское закрытие.
void CloseScanner(bool emitCancelled)
{
	if (!g_scanner)
		return;

	[g_scanner stop];
	g_scanner = nil;

	if (emitCancelled) {

		if (BarcodeScannerAddIn* owner = g_owner.load())
			owner->EmitEvent(u"ScanCancelled", "");

	}
}

// Главный поток. Создаёт и запускает сканер; при неудаче — ScanCancelled,
// чтобы вызывающая форма не осталась в ожидании.
void ShowScanner()
{
	if (g_scanner)
		return;

	BSZScanner* scanner = [[BSZScanner alloc] init];

	if ([scanner showAndStart]) {
		g_scanner = scanner;
		return;
	}

	if (BarcodeScannerAddIn* owner = g_owner.load())
		owner->EmitEvent(u"ScanCancelled", "");
}

} // namespace

namespace bsz::ios {

StartScanResult StartScanning(IAddInDefBase* /*connect*/, BarcodeScannerAddIn* owner,
	const std::string& settingsJson)
{
	g_owner.store(owner);
	g_autoClose = JsonFlag(settingsJson, "autoClose", true);
	g_torchOnStart = JsonFlag(settingsJson, "torch", false);

	const AVAuthorizationStatus status =
		[AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo];

	if (status == AVAuthorizationStatusDenied || status == AVAuthorizationStatusRestricted) {
		NSLog(@"BarcodeScannerZXing: camera access denied");
		return StartScanResult::Failed;
	}

	if (status == AVAuthorizationStatusNotDetermined) {

		[AVCaptureDevice requestAccessForMediaType:AVMediaTypeVideo
			completionHandler:^(BOOL granted) {
				RunOnMain(^{
					if (granted) {
						ShowScanner();
					} else if (BarcodeScannerAddIn* owner = g_owner.load()) {
						owner->EmitEvent(u"ScanCancelled", "");
					}
				});
			}];

		return StartScanResult::Started;
	}

	RunOnMain(^{
		ShowScanner();
	});

	return StartScanResult::Started;
}

bool StopScanning()
{
	g_owner.store(nullptr);

	RunOnMain(^{
		CloseScanner(false);
	});

	return true;
}

bool SetTorch(bool on)
{
	// Синхронный ответ нужен вызывающему BSL — торч переключаем на месте:
	// AVCaptureDevice потокобезопасен при lockForConfiguration.
	BSZScanner* scanner = g_scanner;

	if (!scanner)
		return false;

	return [scanner setTorch:on];
}

} // namespace bsz::ios
