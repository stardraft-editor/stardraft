#pragma once
// A map's picture (preview::render, RGBA) as a CGImage and as PNG data, for the Quick Look extensions and the
// editor's Spotlight entries (macOS only; Objective-C++ with ARC).

#import <CoreGraphics/CoreGraphics.h>
#import <Foundation/Foundation.h>
#import <ImageIO/ImageIO.h>

#include "stardraft/terrain.h"

namespace stardraft::preview {

// A new CGImage (the caller releases it), or NULL.
inline CGImageRef makeCGImage(const Image& img) {
    if (img.width <= 0 || img.height <= 0 || img.rgba.size() < size_t(img.width) * size_t(img.height) * 4) return NULL;
    CFDataRef data = CFDataCreate(NULL, img.rgba.data(), CFIndex(img.rgba.size()));
    CGDataProviderRef provider = CGDataProviderCreateWithCFData(data);
    CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGImageRef cg = CGImageCreate(size_t(img.width), size_t(img.height), 8, 32, size_t(img.width) * 4, space,
                                  kCGBitmapByteOrderDefault | kCGImageAlphaNoneSkipLast, provider, NULL, false,
                                  kCGRenderingIntentDefault);
    CGColorSpaceRelease(space);
    CGDataProviderRelease(provider);
    CFRelease(data);
    return cg;
}

inline NSData* pngData(const Image& img) {
    CGImageRef cg = makeCGImage(img);
    if (!cg) return nil;
    NSMutableData* out = [NSMutableData data];
    CGImageDestinationRef dest = CGImageDestinationCreateWithData((__bridge CFMutableDataRef)out, CFSTR("public.png"), 1, NULL);
    bool ok = false;
    if (dest) {
        CGImageDestinationAddImage(dest, cg, NULL);
        ok = CGImageDestinationFinalize(dest);
        CFRelease(dest);
    }
    CGImageRelease(cg);
    return ok ? out : nil;
}

}  // namespace stardraft::preview
