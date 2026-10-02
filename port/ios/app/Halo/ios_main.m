// The app's entry point. SDL_main.h turns main() into SDL_main, which SDL
// calls on the main thread once UIKit has started; the game's own main()
// (source/shell/shell_xbox.c) is compiled as halo_main.

#import <UIKit/UIKit.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int halo_main(void);

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    @autoreleasepool {
        // the game data (maps/) and config.toml live in Documents, which the
        // Files app shows; the game looks for maps/ in the current directory
        NSString *documents = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES).firstObject;
        setenv("HALO_DATA_ROOT", documents.fileSystemRepresentation, 1);
        chdir(documents.fileSystemRepresentation);

        // saves and the z:\ cache go under the save root, which is
        // $XDG_DATA_HOME/halo-linux (xbox_files.c); the default, ~/.local/share,
        // is in the container's read-only top level on a device
        NSString *support = NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, NSUserDomainMask, YES).firstObject;
        [NSFileManager.defaultManager createDirectoryAtPath:support withIntermediateDirectories:YES attributes:nil error:NULL];
        setenv("XDG_DATA_HOME", support.fileSystemRepresentation, 1);

        // the game draws 480 lines at the display's shape (d3d8_gl.c
        // screen_mode_choose); the Android app passes the same
        CGRect bounds = UIScreen.mainScreen.bounds;
        CGFloat longer = MAX(bounds.size.width, bounds.size.height);
        CGFloat shorter = MIN(bounds.size.width, bounds.size.height);
        if (shorter > 0) {
            char width[16];
            snprintf(width, sizeof(width), "%ld", (long)(480.0 * longer / shorter));
            setenv("HALO_DISPLAY_WIDTH", width, 1);
        }
    }
    return halo_main();
}
