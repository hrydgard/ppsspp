#import <Foundation/Foundation.h>
#import <StoreKit/StoreKit.h>

@interface IAPManager : NSObject <SKProductsRequestDelegate, SKPaymentTransactionObserver>

+ (instancetype)sharedIAPManager;

- (void)buyGoldWithRequestID:(int)requestID;
- (void)restorePurchasesWithRequestID:(int)requestID;
- (BOOL)isGoldUnlocked;
- (void)startObserving;

@end
