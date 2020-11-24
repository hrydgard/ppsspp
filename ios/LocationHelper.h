#import <Foundation/Foundation.h>
#import <CoreLocation/CoreLocation.h>

@protocol LocationHandlerDelegate <NSObject>
@required
- (void) SetGpsDataIOS:(CLLocation*)newLocation;
@end

@interface LocationHelper : NSObject<CLLocationManagerDelegate> {
    CLLocationManager *locationManager;
}

@property(nonatomic,strong) id<LocationHandlerDelegate> delegate;

- (void) startLocationUpdates;
- (void) stopLocationUpdates;

@end
