/* Copyright (c) 2018, Curtis McEnroe <programble@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#import <Cocoa/Cocoa.h>
#import <stdint.h>
#import <stdlib.h>

extern int init(int argc, char *argv[]);
extern void draw(uint32_t *buf, size_t xres, size_t yres);
extern void input(char in);

#define WIDTH (640)
#define HEIGHT (480)

static size_t size = 4 * WIDTH * HEIGHT;
static uint32_t buf[WIDTH * HEIGHT];

@interface BufferView : NSView
@end

@implementation BufferView
- (void) drawRect: (NSRect) dirtyRect {
    CGContextRef ctx = [[NSGraphicsContext currentContext] CGContext];
    CGColorSpaceRef rgb = CGColorSpaceCreateDeviceRGB();
    CGDataProviderRef data = CGDataProviderCreateWithData(NULL, buf, size, NULL);
    CGImageRef image = CGImageCreate(
        WIDTH,
        HEIGHT,
        8,
        32,
        WIDTH * 4,
        rgb,
        kCGBitmapByteOrder32Little | kCGImageAlphaNoneSkipFirst,
        data,
        NULL,
        false,
        kCGRenderingIntentDefault
    );
    CGContextDrawImage(ctx, CGRectMake(0, 0, WIDTH, HEIGHT), image);
    CGImageRelease(image);
    CGDataProviderRelease(data);
    CGColorSpaceRelease(rgb);
}

- (BOOL) acceptsFirstResponder {
    return YES;
}

- (void) keyDown: (NSEvent *) event {
    char in;
    BOOL converted = [
        [event characters]
        getBytes: &in
        maxLength: 1
        usedLength: NULL
        encoding: NSASCIIStringEncoding
        options: 0
        range: NSMakeRange(0, 1)
        remainingRange: NULL
    ];
    if (converted) {
        input(in);
        draw(buf, WIDTH, HEIGHT);
        [self setNeedsDisplay: YES];
    }
}
@end

@interface Delegate : NSObject <NSApplicationDelegate>
@end

@implementation Delegate
- (BOOL) applicationShouldTerminateAfterLastWindowClosed: (NSApplication *) sender {
    return YES;
}
@end

int main(int argc, char *argv[]) {
    int error = init(argc, argv);
    if (error) return error;

    [NSApplication sharedApplication];
    [NSApp setActivationPolicy: NSApplicationActivationPolicyRegular];
    [NSApp setDelegate: [Delegate new]];

    NSString *name = [[NSProcessInfo processInfo] processName];
    NSMenu *menu = [NSMenu new];
    NSMenuItem *quit = [
        [NSMenuItem alloc]
        initWithTitle: [@"Quit " stringByAppendingString: name]
        action: @selector(terminate:)
        keyEquivalent: @"q"
    ];
    [menu addItem: quit];
    NSMenuItem *menuItem = [NSMenuItem new];
    [menuItem setSubmenu: menu];
    [NSApp setMainMenu: [NSMenu new]];
    [[NSApp mainMenu] addItem: menuItem];

    NSUInteger style = NSTitledWindowMask
        | NSClosableWindowMask
        | NSMiniaturizableWindowMask
        | NSResizableWindowMask;
    NSWindow *window = [
        [NSWindow alloc]
        initWithContentRect: NSMakeRect(0, 0, WIDTH, HEIGHT)
        styleMask: style
        backing: NSBackingStoreBuffered
        defer: YES
    ];
    [window setTitle: name];
    [window center];

    BufferView *view = [[BufferView alloc] initWithFrame: [window frame]];
    [window setContentView: view];

    draw(buf, WIDTH, HEIGHT);

    [window makeKeyAndOrderFront: nil];
    [NSApp activateIgnoringOtherApps: YES];
    [NSApp run];
}
