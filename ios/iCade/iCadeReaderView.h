/*
 Copyright (C) 2011 by Stuart Carnie
 
 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:
 
 The above copyright notice and this permission notice shall be included in
 all copies or substantial portions of the Software.
 
 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 THE SOFTWARE.
 */

#import <UIKit/UIKit.h>
#import "iCadeState.h"

/*
 UP ON,OFF  = w,e
 RT ON,OFF  = d,c
 DN ON,OFF  = x,z
 LT ON,OFF  = a,q
 A  ON,OFF  = y,t
 B  ON,OFF  = h,r
 C  ON,OFF  = u,f
 D  ON,OFF  = j,n
 E  ON,OFF  = i,m
 F  ON,OFF  = k,p
 G  ON,OFF  = o,g
 H  ON,OFF  = l,v
*/

@protocol iCadeEventDelegate <NSObject>

@optional
- (void)stateChanged:(iCadeState)state;
- (void)buttonDown:(iCadeState)button;
- (void)buttonUp:(iCadeState)button;

@end

@interface iCadeReaderView : UIView<UIKeyInput> {
    UIView                  *inputView;
    iCadeState              _iCadeState;
    id<iCadeEventDelegate>  _delegate;
    
    struct {
        bool stateChanged:1;
        bool buttonDown:1;
        bool buttonUp:1;
    } _delegateFlags;
}

@property (nonatomic, assign) iCadeState iCadeState;
@property (nonatomic, assign) id<iCadeEventDelegate> delegate;
@property (nonatomic, assign) BOOL active;

@end
