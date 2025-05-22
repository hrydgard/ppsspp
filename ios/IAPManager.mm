#import "IAPManager.h"
#import <UIKit/UIKit.h>
#include "Common/System/Request.h"
#include "Common/Log.h"

#include "../ppsspp_config.h"

#if PPSSPP_PLATFORM(IOS_APP_STORE)

// Only one operation can be in progress at once.
@implementation IAPManager {
	SKProduct *_goldProduct;
	int _pendingRequestID;
}

+ (instancetype)sharedIAPManager {
	static IAPManager *shared = nil;
	static dispatch_once_t onceToken;
	dispatch_once(&onceToken, ^{
		shared = [[self alloc] init];
	});
	return shared;
}

- (instancetype)init {
	if (self = [super init]) {
		[[SKPaymentQueue defaultQueue] addTransactionObserver:self];
	}
	return self;
}

- (void)startObserving {
	[[SKPaymentQueue defaultQueue] addTransactionObserver:self];
}

- (BOOL)isGoldUnlocked {
	return [[NSUserDefaults standardUserDefaults] boolForKey:@"isGold"];
}

- (void)buyGoldWithRequestID:(int)requestID {
	if (_pendingRequestID) {
		ERROR_LOG(Log::IAP, "A transaction is pending. Failing the new request.");
		g_requestManager.PostSystemFailure(requestID);
		return;
	}
	_pendingRequestID = requestID;

	if ([SKPaymentQueue canMakePayments]) {
		NSSet *productIds = [NSSet setWithObject:@"org.ppsspp.gold"];
		SKProductsRequest *request = [[SKProductsRequest alloc] initWithProductIdentifiers:productIds];
		request.delegate = self;
		[request start];
	} else {
		NSLog(@"[IAPManager] In-App Purchases are disabled (requestID: %d)", requestID);
		g_requestManager.PostSystemFailure(requestID);
	}
}

- (void)restorePurchasesWithRequestID:(int)requestID {
	if (_pendingRequestID) {
		ERROR_LOG(Log::IAP, "A transaction is pending. Failing the new request.");
		g_requestManager.PostSystemFailure(requestID);
		return;
	}
	_pendingRequestID = requestID;
	INFO_LOG(Log::IAP, "Restoring purchases (id=%d)", requestID);
	// NOTE: This is deprecated, but StoreKit 2 is swift only. We'll keep using it until
	// there's a replacement.
	[[SKPaymentQueue defaultQueue] restoreCompletedTransactions];
}

- (void)paymentQueueRestoreCompletedTransactionsFinished:(SKPaymentQueue *)queue {
	NSLog(@"Restore completed successfully (requestID: %d)", _pendingRequestID);
	g_requestManager.PostSystemSuccess(_pendingRequestID, "", 0);
	_pendingRequestID = 0;
	// Notify your app/UI here
}

- (void)paymentQueue:(SKPaymentQueue *)queue restoreCompletedTransactionsFailedWithError:(NSError *)error {
	NSLog(@"Restore failed (requestID: %d): %@", _pendingRequestID, error.localizedDescription);
	// Notify failure to game layer
	g_requestManager.PostSystemFailure(_pendingRequestID);
	_pendingRequestID = 0;
}

- (void)productsRequest:(SKProductsRequest *)request didReceiveResponse:(SKProductsResponse *)response {
	_goldProduct = response.products.firstObject;
	if (_goldProduct) {
		// Received a valid product. Send a payment.
		SKPayment *payment = [SKPayment paymentWithProduct:_goldProduct];
		[[SKPaymentQueue defaultQueue] addPayment:payment];
	} else {
		NSLog(@"[IAPManager] Gold product not found (requestID: %d)", _pendingRequestID);
		g_requestManager.PostSystemFailure(_pendingRequestID);
		_pendingRequestID = 0;
	}
}

- (void)paymentQueue:(SKPaymentQueue *)queue updatedTransactions:(NSArray<SKPaymentTransaction *> *)transactions {
	for (SKPaymentTransaction *transaction in transactions) {
		switch (transaction.transactionState) {
			case SKPaymentTransactionStatePurchased:
			case SKPaymentTransactionStateRestored:
				INFO_LOG(Log::IAP, transaction.transactionState == SKPaymentTransactionStatePurchased ? "IAP Purchase" : "IAP Restore");
				// Perform the unlock (updaing the variable and switching the icon).
				[self unlockGold];
				[[SKPaymentQueue defaultQueue] finishTransaction:transaction];
				g_requestManager.PostSystemSuccess(_pendingRequestID, "", 0);
				_pendingRequestID = 0;
				break;
			case SKPaymentTransactionStateFailed:
				NSLog(@"[IAPManager] Purchase failed (requestID: %d): %@", _pendingRequestID, transaction.error.localizedDescription);
				[[SKPaymentQueue defaultQueue] finishTransaction:transaction];
				// Optionally post failure callback here
				g_requestManager.PostSystemFailure(_pendingRequestID);
				_pendingRequestID = 0;
				break;
			default:
				break;
		}
	}
}

- (void)unlockGold {
	INFO_LOG(Log::UI, "Unlocking gold reward!");

	// Write to user defaults to store the status.
	[[NSUserDefaults standardUserDefaults] setBool:YES forKey:@"isGold"];
	[[NSUserDefaults standardUserDefaults] synchronize];

	[self updateIcon];

	NSLog(@"[IAPManager] Gold unlocked!");
}

- (void)updateIcon {
	NSString *icon = nil;
	if ([self isGoldUnlocked]) {
		icon = @"AppIconGold";
	}

	NSLog(@"updateIcon called with %@", icon);

	if (![UIApplication sharedApplication]) {
		NSLog(@"IAPManager: Application not initialized");
		return;
	}

	NSLog(@"Current icon name: %@", [[UIApplication sharedApplication] alternateIconName]);

	if (icon) {
		NSLog(@"IAPManager about to update icon to %@", icon);
	} else {
		NSLog(@"IAPManager about to reset the icon");
	}
	NSLog(@"IAPManager: App state: %ld", (long)[UIApplication sharedApplication].applicationState);

	if ([[UIApplication sharedApplication] supportsAlternateIcons]) {
		[[UIApplication sharedApplication] setAlternateIconName:icon
											  completionHandler:^(NSError * _Nullable error) {
			if (error) {
				NSLog(@"[IAPManager] Failed to set Gold icon to %@: %@", icon, error.localizedDescription);
			} else {
				NSLog(@"Icon update succeeded.");
			}
		}];
		NSLog(@"Icon update to %@ dispatched, waiting for response.", icon);
	} else {
		NSLog(@"Application doesn't support alternate icons.");
	}
}

#endif

@end
