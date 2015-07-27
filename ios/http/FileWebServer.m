//
//  WebServer.m
//  PPSSPP
//
//  Created by Daniel Gillespie on 7/25/15.
//

#import "FileWebServer.h"

@implementation FileWebServer

-(id)init {
    webServer = [[GCDWebUploader alloc] initWithUploadDirectory: [self getDocumentDirectory]];
    webServer.delegate = self;
    webServer.allowHiddenItems = NO;
    
    return self;
}

-(NSString*) getDocumentDirectory {
    NSArray *searchPaths = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES);
    NSString *documentPath = [searchPaths objectAtIndex: 0];
    
    return documentPath;
}

-(NSString*) startServer {
    //[[UIApplication sharedApplication] setIdleTimerDisabled: YES];
    
    // Check to see if we are connected to WiFi. Cannot continue otherwise.
    Reachability *reachability = [Reachability reachabilityForInternetConnection];
    [reachability startNotifier];
    
    NetworkStatus status = [reachability currentReachabilityStatus];
    
    if (status == ReachableViaWiFi)
    {
        // connected via wifi, let's continue
        
        
        [webServer start];
        NSString *ipAddress = [self getIPAddress];

#if TARGET_IPHONE_SIMULATOR
        ipAddress = [ipAddress stringByAppendingString: @":8080"];
#endif
        
        return [NSString stringWithFormat: @"http://%@/", ipAddress];
    }
    
    return NULL;
}

-(void) stopServer {
    //[[UIApplication sharedApplication] setIdleTimerDisabled: NO];
    [webServer stop];
}


- (NSString *)getIPAddress {
    
    NSString *address = @"error";
    struct ifaddrs *interfaces = NULL;
    struct ifaddrs *temp_addr = NULL;
    int success = 0;
    // retrieve the current interfaces - returns 0 on success
    success = getifaddrs(&interfaces);
    if (success == 0) {
        // Loop through linked list of interfaces
        temp_addr = interfaces;
        while(temp_addr != NULL) {
            if(temp_addr->ifa_addr->sa_family == AF_INET) {
                // Check if interface is en0 which is the wifi connection on the iPhone
                if([[NSString stringWithUTF8String:temp_addr->ifa_name] isEqualToString:@"en0"]) {
                    // Get NSString from C String
                    address = [NSString stringWithUTF8String:inet_ntoa(((struct sockaddr_in *)temp_addr->ifa_addr)->sin_addr)];
                    
                }
                
            }
            
            temp_addr = temp_addr->ifa_next;
        }
    }
    // Free memory
    freeifaddrs(interfaces);
    return address;
    
}



#pragma mark - Web Server Delegate

- (void)webUploader:(GCDWebUploader*)uploader didUploadFileAtPath:(NSString*)path {
    NSLog(@"[UPLOAD] %@", path);
}

- (void)webUploader:(GCDWebUploader*)uploader didMoveItemFromPath:(NSString*)fromPath toPath:(NSString*)toPath {
    NSLog(@"[MOVE] %@ -> %@", fromPath, toPath);
}

- (void)webUploader:(GCDWebUploader*)uploader didDeleteItemAtPath:(NSString*)path {
    NSLog(@"[DELETE] %@", path);
}

- (void)webUploader:(GCDWebUploader*)uploader didCreateDirectoryAtPath:(NSString*)path {
    NSLog(@"[CREATE] %@", path);
}

@end
