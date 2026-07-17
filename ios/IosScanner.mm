// Этап 0: прототип UI-риска — полноэкранный оверлей поверх окна 1С без камеры.
// Снимает главный вопрос iOS-порта: можно ли из Native API ВК показать свой
// интерфейс и доставить событие в BSL. Камера и декодирование — этап 1.

#include "IosScanner.h"

#import <UIKit/UIKit.h>

#include <atomic>

#include "BarcodeScannerAddIn.h"

namespace {

// Компонента может умереть (Done/DestroyObject), пока оверлей открыт, —
// поэтому владелец хранится атомарно и обнуляется синхронно из StopScanning.
std::atomic<BarcodeScannerAddIn*> g_owner{nullptr};

// Только главный поток.
UIView* g_overlay = nil;
NSObject* g_actions = nil;

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

	// Приложение платформы без SceneDelegate — классический путь.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
	if (app.keyWindow)
		return app.keyWindow;

	return app.windows.firstObject;
#pragma clang diagnostic pop
}

// Главный поток. Закрывает оверлей; emitCancelled — пользовательское закрытие.
void CloseOverlay(bool emitCancelled)
{
	if (!g_overlay)
		return;

	[g_overlay removeFromSuperview];
	g_overlay = nil;
	g_actions = nil;

	if (emitCancelled) {

		if (BarcodeScannerAddIn* owner = g_owner.load())
			owner->EmitEvent(u"ScanCancelled", "");

	}
}

} // namespace

// Target-action для кнопки закрытия (UIKit требует objc-объект).
@interface BSZOverlayActions : NSObject
- (void)closeTapped:(id)sender;
@end

@implementation BSZOverlayActions
- (void)closeTapped:(id)sender
{
	CloseOverlay(true);
}
@end

namespace bsz::ios {

StartScanResult StartScanning(IAddInDefBase* /*connect*/, BarcodeScannerAddIn* owner,
	const std::string& /*settingsJson*/)
{
	g_owner.store(owner);

	RunOnMain(^{
		if (g_overlay)
			return;

		UIWindow* window = KeyWindow();

		if (!window) {
			NSLog(@"BarcodeScannerZXing: key window not found, overlay is not shown");

			if (BarcodeScannerAddIn* current = g_owner.load())
				current->EmitEvent(u"ScanCancelled", "");

			return;
		}

		UIView* overlay = [[UIView alloc] initWithFrame:window.bounds];
		overlay.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
		overlay.backgroundColor = [UIColor colorWithWhite:0.0 alpha:0.82];

		UILabel* title = [[UILabel alloc] initWithFrame:CGRectZero];
		title.text = @"BarcodeScannerZXing";
		title.textColor = UIColor.whiteColor;
		title.font = [UIFont boldSystemFontOfSize:22.0];
		title.textAlignment = NSTextAlignmentCenter;

		UILabel* subtitle = [[UILabel alloc] initWithFrame:CGRectZero];
		subtitle.text = @"Прототип iOS: интерфейс ВК поверх окна 1С работает";
		subtitle.textColor = [UIColor colorWithWhite:1.0 alpha:0.75];
		subtitle.font = [UIFont systemFontOfSize:15.0];
		subtitle.textAlignment = NSTextAlignmentCenter;
		subtitle.numberOfLines = 0;

		BSZOverlayActions* actions = [[BSZOverlayActions alloc] init];

		UIButton* close = [UIButton buttonWithType:UIButtonTypeSystem];
		[close setTitle:@"Закрыть" forState:UIControlStateNormal];
		[close setTitleColor:UIColor.whiteColor forState:UIControlStateNormal];
		close.titleLabel.font = [UIFont systemFontOfSize:17.0];
		close.layer.borderColor = [UIColor colorWithWhite:1.0 alpha:0.6].CGColor;
		close.layer.borderWidth = 1.0;
		close.layer.cornerRadius = 22.0;
		[close addTarget:actions action:@selector(closeTapped:) forControlEvents:UIControlEventTouchUpInside];

		const CGFloat width = overlay.bounds.size.width;
		const CGFloat height = overlay.bounds.size.height;
		title.frame = CGRectMake(24.0, height * 0.40 - 40.0, width - 48.0, 30.0);
		subtitle.frame = CGRectMake(24.0, height * 0.40, width - 48.0, 44.0);
		close.frame = CGRectMake(width / 2.0 - 100.0, height - 120.0, 200.0, 44.0);

		title.autoresizingMask = UIViewAutoresizingFlexibleWidth
			| UIViewAutoresizingFlexibleTopMargin | UIViewAutoresizingFlexibleBottomMargin;
		subtitle.autoresizingMask = title.autoresizingMask;
		close.autoresizingMask = UIViewAutoresizingFlexibleLeftMargin
			| UIViewAutoresizingFlexibleRightMargin | UIViewAutoresizingFlexibleTopMargin;

		[overlay addSubview:title];
		[overlay addSubview:subtitle];
		[overlay addSubview:close];
		[window addSubview:overlay];

		g_overlay = overlay;
		g_actions = actions;
		NSLog(@"BarcodeScannerZXing: overlay shown (stage 0 prototype)");
	});

	return StartScanResult::Started;
}

bool StopScanning()
{
	g_owner.store(nullptr);

	RunOnMain(^{
		CloseOverlay(false);
	});

	return true;
}

bool SetTorch(bool /*on*/)
{
	return false;
}

} // namespace bsz::ios
