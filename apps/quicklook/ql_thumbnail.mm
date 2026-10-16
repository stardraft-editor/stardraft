// StarDraftThumbnail.appex: Finder's icon of a map (.scm, .scx) is its minimap picture (map_preview.h). A Quick Look
// thumbnail extension (macOS 10.15 and later), in the app's Contents/PlugIns. Built with ARC.

#import <Foundation/Foundation.h>
#import <QuickLookThumbnailing/QuickLookThumbnailing.h>

#include <memory>
#include <string>

#include "map_preview.h"
#include "ql_image.h"

namespace preview = stardraft::preview;

@interface StarDraftThumbnailProvider : QLThumbnailProvider
@end

@implementation StarDraftThumbnailProvider

- (void)provideThumbnailForFileRequest:(QLFileThumbnailRequest*)request
                     completionHandler:(void (^)(QLThumbnailReply* _Nullable, NSError* _Nullable))handler {
    stardraft::Image img;
    preview::MapInfo info;
    std::string error;
    const CGFloat side = MAX(request.maximumSize.width, request.maximumSize.height) * MAX(request.scale, 1.0);
    if (!preview::renderFile(request.fileURL.fileSystemRepresentation, preview::tileColorsFolder(), int(MIN(side, 1024.0)), img, info,
                             nullptr, error)) {
        handler(nil, [NSError errorWithDomain:NSCocoaErrorDomain
                                         code:NSFileReadCorruptFileError
                                     userInfo:@{NSLocalizedDescriptionKey : [NSString stringWithUTF8String:error.c_str()] ?: @""}]);
        return;
    }
    // (the block keeps the picture until Quick Look has drawn it)
    std::shared_ptr<CGImage> cg(preview::makeCGImage(img), [](CGImageRef p) { if (p) CGImageRelease(p); });
    if (!cg) {
        handler(nil, [NSError errorWithDomain:NSCocoaErrorDomain code:NSFileReadUnknownError userInfo:nil]);
        return;
    }
    const CGSize max = request.maximumSize;
    const CGFloat fit = MIN(max.width / img.width, max.height / img.height);
    const CGSize size = CGSizeMake(MAX(1.0, img.width * fit), MAX(1.0, img.height * fit));
    QLThumbnailReply* reply = [QLThumbnailReply replyWithContextSize:size
                                                      drawingBlock:^BOOL(CGContextRef ctx) {
                                                    CGContextSetInterpolationQuality(ctx, kCGInterpolationNone);  // (the tiles stay sharp)
                                                    CGContextDrawImage(ctx, CGRectMake(0, 0, size.width, size.height), cg.get());
                                                    return YES;
                                                }];
    handler(reply, nil);
}

@end
