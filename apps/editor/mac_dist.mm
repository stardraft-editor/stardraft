// What the macOS app gives the system (mac_dist.h): the Quick Look extensions' tile colors, the maps in Spotlight
// (CoreSpotlight) and the bundle's report. Built with ARC (CMakeLists.txt).

#import <AppKit/AppKit.h>
#import <CoreSpotlight/CoreSpotlight.h>
#import <Foundation/Foundation.h>
#import <Security/Security.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#import <objc/runtime.h>

#include "mac_dist.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

#include "map_preview.h"
#include "ql_image.h"

namespace fs = std::filesystem;
namespace preview = stardraft::preview;

namespace {

NSString* nsText(const std::string& s) { return [NSString stringWithUTF8String:s.c_str()] ?: @""; }
std::string utf8(NSString* s) { return s ? std::string([s UTF8String]) : std::string(); }

NSString* const kDomain = @"maps";  // the Spotlight domain of the maps' entries

NSString* mapTypeId(const fs::path& map) {
    std::string ext = map.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return ext == ".scx" ? @"io.github.stardraft-editor.map-scx" : @"io.github.stardraft-editor.map-scm";
}

dispatch_queue_t indexQueue() {
    static dispatch_queue_t q = dispatch_queue_create("io.github.stardraft-editor.spotlight", DISPATCH_QUEUE_SERIAL);
    return q;
}

// The map's Spotlight entry: its name (else the file's name), its description, the file and its picture.
CSSearchableItem* mapItem(const fs::path& map, std::string& error) {
    preview::MapInfo info;
    stardraft::Image img;
    bool table = false;
    const bool drawn = preview::renderFile(map, preview::tileColorsFolder(), 256, img, info, &table, error);
    if (!drawn && info.width == 0) return nil;  // (no map)
    NSString* file = nsText(map.filename().string());
    UTType* type = [UTType typeWithIdentifier:mapTypeId(map)] ?: UTTypeData;
    CSSearchableItemAttributeSet* a = [[CSSearchableItemAttributeSet alloc] initWithContentType:type];
    a.title = info.name.empty() ? file : nsText(info.name);
    a.displayName = a.title;
    a.contentDescription = info.description.empty() ? nil : nsText(info.description);
    a.contentURL = [NSURL fileURLWithPath:nsText(map.string())];
    a.keywords = @[ file, @"StarCraft", nsText(stardraft::tilesetName(info.era)) ];
    if (drawn) a.thumbnailData = preview::pngData(img);
    return [[CSSearchableItem alloc] initWithUniqueIdentifier:nsText(map.string()) domainIdentifier:kDomain attributeSet:a];
}

void (*gOpen)(const std::string&) = nullptr;

}  // namespace

void macMapOpened(const fs::path& map, const std::vector<uint32_t>& tileColors, uint16_t era) {
    const fs::path dir = preview::tileColorsFolder();
    if (!tileColors.empty() && !dir.empty()) {
        const fs::path file = preview::tileColorsFile(dir, era);
        std::vector<uint32_t> old;
        std::string err;
        if ((!preview::readTileColors(file, old) || old != tileColors) && !preview::writeTileColors(file, tileColors, err))
            std::fprintf(stderr, "quick look: %s\n", err.c_str());
    }
    if (![CSSearchableIndex isIndexingAvailable]) return;
    const fs::path path = map;
    dispatch_async(indexQueue(), ^{
        @autoreleasepool {
            std::string err;
            CSSearchableItem* item = mapItem(path, err);
            if (!item) return;
            [[CSSearchableIndex defaultSearchableIndex] indexSearchableItems:@[ item ]
                                                           completionHandler:^(NSError* e) {
                                                               if (e) std::fprintf(stderr, "spotlight: %s\n", utf8(e.localizedDescription).c_str());
                                                           }];
        }
    });
}

void macHandleSpotlightOpens(void (*open)(const std::string&)) {
    gOpen = open;
    id delegate = [NSApp delegate];
    if (!delegate) return;
    SEL sel = @selector(application:continueUserActivity:restorationHandler:);
    if ([delegate respondsToSelector:sel]) return;  // (SDL's delegate has its own: it stays)
    const objc_method_description d = protocol_getMethodDescription(@protocol(NSApplicationDelegate), sel, NO, YES);
    if (!d.types) return;
    IMP imp = imp_implementationWithBlock(^BOOL(id, NSApplication*, NSUserActivity* activity, id) {
        if (![activity.activityType isEqualToString:CSSearchableItemActionType]) return NO;
        NSString* path = activity.userInfo[CSSearchableItemActivityIdentifier];
        if (!path || !gOpen) return NO;
        gOpen(utf8(path));
        return YES;
    });
    class_addMethod([delegate class], sel, imp, d.types);
}

std::string macSpotlightCheck(const fs::path& map) {
    std::string out;
    @autoreleasepool {
        std::string err;
        CSSearchableItem* item = mapItem(map, err);
        if (!item) return "spotlight: no entry (WRONG): " + err + "\n";
        CSSearchableItemAttributeSet* a = item.attributeSet;
        out += "spotlight: title " + utf8(a.title) + "\n";
        out += "spotlight: description " + std::to_string(utf8(a.contentDescription).size()) + " bytes\n";
        out += "spotlight: picture " + std::to_string(a.thumbnailData.length) + " bytes of PNG\n";
        out += std::string("spotlight: type ") + utf8(a.contentType) + "\n";
        if (![CSSearchableIndex isIndexingAvailable]) return out + "spotlight: indexing not available on this Mac\n";
        __block NSError* indexed = nil;
        __block NSError* removed = nil;
        dispatch_semaphore_t done = dispatch_semaphore_create(0);
        CSSearchableIndex* index = [CSSearchableIndex defaultSearchableIndex];
        [index indexSearchableItems:@[ item ] completionHandler:^(NSError* e) { indexed = e; dispatch_semaphore_signal(done); }];
        const bool inTime = dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, 20 * NSEC_PER_SEC)) == 0;
        out += std::string("spotlight: indexed ") + (!inTime ? "no answer in 20 s" : indexed ? utf8(indexed.localizedDescription) : "yes") + "\n";
        // (a test leaves nothing in the user's index)
        [index deleteSearchableItemsWithIdentifiers:@[ item.uniqueIdentifier ] completionHandler:^(NSError* e) {
            removed = e;
            dispatch_semaphore_signal(done);
        }];
        const bool goneInTime = dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, 20 * NSEC_PER_SEC)) == 0;
        out += std::string("spotlight: removed ") + (!goneInTime ? "no answer in 20 s" : removed ? utf8(removed.localizedDescription) : "yes") + "\n";
    }
    return out;
}

namespace {

std::string joined(NSArray* a) {
    std::string s;
    for (id x in a) {
        if (!s.empty()) s += ",";
        s += utf8([x description]);
    }
    return s;
}

std::string signatureReport(NSString* bundlePath, const char* who) {
    std::string out;
    SecStaticCodeRef code = NULL;
    if (SecStaticCodeCreateWithPath((__bridge CFURLRef)[NSURL fileURLWithPath:bundlePath], kSecCSDefaultFlags, &code) != errSecSuccess || !code)
        return std::string("bundle: ") + who + " signature: none\n";
    CFDictionaryRef info = NULL;
    if (SecCodeCopySigningInformation(code, SecCSFlags(kSecCSSigningInformation), &info) == errSecSuccess && info) {
        NSDictionary* d = (__bridge NSDictionary*)info;
        const uint32_t flags = [d[(__bridge NSString*)kSecCodeInfoFlags] unsignedIntValue];
        NSString* team = d[(__bridge NSString*)kSecCodeInfoTeamIdentifier];
        NSDictionary* ent = d[(__bridge NSString*)kSecCodeInfoEntitlementsDict];
        NSArray* keys = [[ent allKeys] sortedArrayUsingSelector:@selector(compare:)];
        out += std::string("bundle: ") + who + " signature: " + ((flags & 0x2) ? std::string("ad hoc") : team ? "team " + utf8(team) : std::string("signed")) +
               ", hardened runtime " + ((flags & 0x10000) ? "yes" : "no") + "\n";  // (kSecCodeSignatureAdhoc, kSecCodeSignatureRuntime)
        out += std::string("bundle: ") + who + " entitlements: " + (keys.count ? joined(keys) : std::string("none")) + "\n";
        CFRelease(info);
    }
    CFRelease(code);
    return out;
}

}  // namespace

std::string macBundleReport() {
    std::string out;
    @autoreleasepool {
        NSBundle* b = [NSBundle mainBundle];
        NSDictionary* d = b.infoDictionary;
        out += "bundle: " + utf8(b.bundlePath.lastPathComponent) + " id " + utf8(d[@"CFBundleIdentifier"]) + " version " +
               utf8(d[@"CFBundleShortVersionString"]) + "\n";
        for (NSDictionary* t in d[@"CFBundleDocumentTypes"]) {
            const bool generated = [t[@"CFBundleTypeIconSystemGenerated"] boolValue];
            out += "bundle: document type " + utf8(t[@"CFBundleTypeName"]) + " (" + joined(t[@"LSItemContentTypes"]) + ") role " +
                   utf8(t[@"CFBundleTypeRole"]) + " rank " + utf8(t[@"LSHandlerRank"]) + " icon " +
                   (generated ? std::string("made by the system") : utf8(t[@"CFBundleTypeIconFile"])) + "\n";
        }
        for (NSDictionary* t in d[@"UTExportedTypeDeclarations"]) {
            NSDictionary* icons = t[@"UTTypeIcons"];
            out += "bundle: exported type " + utf8(t[@"UTTypeIdentifier"]) + " (" +
                   joined(t[@"UTTypeTagSpecification"][@"public.filename-extension"]) + ") conforms to " + joined(t[@"UTTypeConformsTo"]) +
                   " icon text " + utf8(icons[@"UTTypeIconText"]) + " badge " + utf8(icons[@"UTTypeIconBadgeName"]) + "\n";
        }
        NSArray* plugins = [[[NSFileManager defaultManager] contentsOfDirectoryAtPath:b.builtInPlugInsPath error:nil]
            sortedArrayUsingSelector:@selector(compare:)];
        for (NSString* name in plugins) {
            if (![name.pathExtension isEqualToString:@"appex"]) continue;
            NSString* path = [b.builtInPlugInsPath stringByAppendingPathComponent:name];
            NSBundle* p = [NSBundle bundleWithPath:path];
            NSDictionary* ext = p.infoDictionary[@"NSExtension"];
            out += "bundle: plug-in " + utf8(name) + " " + utf8(ext[@"NSExtensionPointIdentifier"]) + " types " +
                   joined(ext[@"NSExtensionAttributes"][@"QLSupportedContentTypes"]) + " class " +
                   utf8(ext[@"NSExtensionPrincipalClass"]) + "\n";
            out += signatureReport(path, utf8(name).c_str());
        }
        out += signatureReport(b.bundlePath, "app");
    }
    return out;
}
