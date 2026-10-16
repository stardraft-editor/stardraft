// StarDraftPreview.appex: the space bar's Quick Look preview of a map (.scm, .scx): its minimap picture
// (map_preview.h), its name, its size and its description. A Quick Look preview extension (macOS 10.15 and later), in
// the app's Contents/PlugIns. The texts are the map's own (and numbers), so nothing here needs a translation. Built
// with ARC.

#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>
#import <Quartz/Quartz.h>

#include <string>

#include "map_preview.h"
#include "ql_image.h"

namespace preview = stardraft::preview;

@interface StarDraftPreviewController : NSViewController <QLPreviewingController>
@end

@implementation StarDraftPreviewController

- (NSString*)nibName {
    return nil;  // (the view is made in code)
}

- (void)loadView {
    self.view = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 640, 720)];
}

static NSTextField* label(NSString* text, NSFont* font, NSColor* color) {
    NSTextField* t = [NSTextField wrappingLabelWithString:text ?: @""];
    t.font = font;
    t.textColor = color;
    t.selectable = YES;
    t.preferredMaxLayoutWidth = 600;  // (the picture's width)
    return t;
}

- (void)preparePreviewOfFileAtURL:(NSURL*)url completionHandler:(void (^)(NSError* _Nullable))handler {
    stardraft::Image img;
    preview::MapInfo info;
    std::string error;
    if (!preview::renderFile(url.fileSystemRepresentation, preview::tileColorsFolder(), 1024, img, info, nullptr, error)) {
        handler([NSError errorWithDomain:NSCocoaErrorDomain
                                    code:NSFileReadCorruptFileError
                                userInfo:@{NSLocalizedDescriptionKey : [NSString stringWithUTF8String:error.c_str()] ?: @""}]);
        return;
    }
    CGImageRef cg = preview::makeCGImage(img);
    const CGFloat side = 600;  // (points: the picture fits in it, as the map's shape is)
    const CGFloat fit = side / MAX(img.width, img.height);
    NSImage* picture = [[NSImage alloc] initWithCGImage:cg size:NSMakeSize(img.width * fit, img.height * fit)];
    if (cg) CGImageRelease(cg);

    NSImageView* iv = [NSImageView imageViewWithImage:picture];
    iv.imageScaling = NSImageScaleProportionallyUpOrDown;
    iv.wantsLayer = YES;
    iv.layer.magnificationFilter = kCAFilterNearest;  // (the tiles stay sharp)

    NSString* name = info.name.empty() ? url.lastPathComponent : [NSString stringWithUTF8String:info.name.c_str()];
    NSString* size = [NSString stringWithFormat:@"%d × %d", info.width, info.height];
    NSMutableArray* views = [NSMutableArray arrayWithObjects:label(name ?: url.lastPathComponent, [NSFont boldSystemFontOfSize:17], NSColor.labelColor),
                                                             label(size, [NSFont systemFontOfSize:13], NSColor.secondaryLabelColor), iv, nil];
    if (!info.description.empty())
        [views addObject:label([NSString stringWithUTF8String:info.description.c_str()], [NSFont systemFontOfSize:13], NSColor.labelColor)];
    NSStackView* stack = [NSStackView stackViewWithViews:views];
    stack.orientation = NSUserInterfaceLayoutOrientationVertical;
    stack.alignment = NSLayoutAttributeLeading;
    stack.spacing = 8;
    stack.edgeInsets = NSEdgeInsetsMake(16, 16, 16, 16);
    stack.translatesAutoresizingMaskIntoConstraints = NO;
    for (NSView* v in self.view.subviews.copy) [v removeFromSuperview];
    [self.view addSubview:stack];
    [NSLayoutConstraint activateConstraints:@[
        [stack.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor],
        [stack.trailingAnchor constraintLessThanOrEqualToAnchor:self.view.trailingAnchor],
        [stack.topAnchor constraintEqualToAnchor:self.view.topAnchor],
        [stack.bottomAnchor constraintLessThanOrEqualToAnchor:self.view.bottomAnchor],
        [iv.widthAnchor constraintEqualToConstant:picture.size.width],
        [iv.heightAnchor constraintEqualToConstant:picture.size.height],
    ]];
    self.preferredContentSize = NSMakeSize(picture.size.width + 32, picture.size.height + 140);
    handler(nil);
}

@end
