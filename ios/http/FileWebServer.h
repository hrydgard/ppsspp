//
//  WebServer.h
//  PPSSPP
//
//  Created by Daniel Gillespie on 7/25/15.
//

#import <Foundation/Foundation.h>

// Networking (GET IP)
#include <ifaddrs.h>
#include <arpa/inet.h>


#import "ios/http/Reachability/Reachability.h"


// Web Server
#import "ios/http/GCDWebUploader/GCDWebUploader.h"



GCDWebUploader *webServer;

@interface FileWebServer : NSObject <GCDWebUploaderDelegate>

// prototypes
-(id)init;
-(NSString*) getDocumentDirectory;
- (NSString *)getIPAddress;
-(NSString*) startServer;
-(void) stopServer;

@end

