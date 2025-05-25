#import "IAPManager.h"
#import <UIKit/UIKit.h>
#import "ViewControllerCommon.h"
#include "Common/System/Request.h"
#include "Common/Log.h"

#include "../ppsspp_config.h"

// Only one operation can be in progress at once.
@implementation IAPManager {
#ifdef USE_IAP
	SKProduct *_goldProduct;
#endif
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
#ifdef USE_IAP
	if (self = [super init]) {
		[[SKPaymentQueue defaultQueue] addTransactionObserver:self];
	}
#endif
	return self;
}

- (void)startObserving {
#ifdef USE_IAP
	[[SKPaymentQueue defaultQueue] addTransactionObserver:self];
#endif
}

- (BOOL)isGoldUnlocked {
#ifdef USE_IAP
	return [[NSUserDefaults standardUserDefaults] boolForKey:@"isGold"];
#else
	return false;
#endif
}

- (void)buyGoldWithRequestID:(int)requestID {
#ifdef USE_IAP
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
#else
	g_requestManager.PostSystemFailure(requestID);
#endif
}

- (void)restorePurchasesWithRequestID:(int)requestID {
#ifdef USE_IAP
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
#else
	g_requestManager.PostSystemFailure(requestID);
#endif
}

#ifdef USE_IAP

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

#endif

- (void)unlockGold {
#ifdef USE_IAP
	INFO_LOG(Log::UI, "Unlocking gold reward!");

	// Write to user defaults to store the status.
	[[NSUserDefaults standardUserDefaults] setBool:YES forKey:@"isGold"];
	[[NSUserDefaults standardUserDefaults] synchronize];

	dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(4.0 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
		[self updateIcon:true];
	});

	NSLog(@"[IAPManager] Gold unlocked!");
#endif
}

static bool SafeStringEqual(NSString *a, NSString *b) {
    return (a == b) || [a isEqualToString:b];
}

- (void)updateIcon:(bool)force {
	NSString *desiredIcon = nil;
	if ([self isGoldUnlocked]) {
		desiredIcon = @"GoldIcon";
	}

	NSLog(@"updateIcon called with %@", desiredIcon);

	if (![UIApplication sharedApplication]) {
		NSLog(@"IAPManager: Application not initialized");
		return;
	}

	NSLog(@"Current icon name: %@", [[UIApplication sharedApplication] alternateIconName]);

	if (desiredIcon) {
		NSLog(@"IAPManager about to update icon to %@ (force=%d)", desiredIcon, (int)force);
	} else {
		NSLog(@"IAPManager about to reset the icon (force=%d)", (int)force);
	}

	if ([[UIApplication sharedApplication] supportsAlternateIcons]) {
		if (force || !SafeStringEqual([UIApplication sharedApplication].alternateIconName, desiredIcon)) {
			// Name not matching, do the update.
			[[UIApplication sharedApplication] setAlternateIconName:desiredIcon
												  completionHandler:^(NSError * _Nullable error) {
				if (error) {
					NSLog(@"[IAPManager] Failed to set Gold icon to %@: %@", desiredIcon, error.localizedDescription);
					[sharedViewController hideKeyboard];
				} else {
					NSLog(@"Icon update succeeded.");
					NSLog(@"Current icon name: %@", [[UIApplication sharedApplication] alternateIconName]);
					// Here we need to call hideKeyboard.
					[sharedViewController hideKeyboard];
				}
			}];
			NSLog(@"Icon update to %@ dispatched, waiting for response.", desiredIcon);
		} else {
			NSLog(@"Icon is already correct: %@", desiredIcon);
		}
	} else {
		NSLog(@"Application doesn't support alternate icons.");
	}
}

@end
