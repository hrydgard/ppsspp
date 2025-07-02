#import <Foundation/Foundation.h>
#import <StoreKit/StoreKit.h>

#include "../ppsspp_config.h"

#if PPSSPP_PLATFORM(IOS_APP_STORE)

@interface IAPManager : NSObject <SKProductsRequestDelegate, SKPaymentTransactionObserver>

+ (instancetype)sharedIAPManager;

- (void)buyGoldWithRequestID:(int)requestID;
- (void)restorePurchasesWithRequestID:(int)requestID;
- (BOOL)isGoldUnlocked;
- (void)startObserving;
- (void)updateIcon:(bool)force;

@end

#endif
