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

// Web Server
#import "ios/http/GCDWebUploader/GCDWebUploader.h"


GCDWebUploader *webServer;

@interface FileWebServer : NSObject <GCDWebUploaderDelegate>

// prototypes
-(id)init;
-(NSString*) getDocumentDirectory;
- (NSString *)getIPAddress;
-(void) startServer;
-(void) stopServer;

@end

