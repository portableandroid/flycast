/*   SDLMain.m - main entry point for our Cocoa-ized SDL app
 Initial Version: Darrell Walisser <dwaliss1@purdue.edu>
 Non-NIB-Code & other changes: Max Horn <max@quendi.de>
 
 Feel free to customize this file to suit your needs
 */
#include <SDL.h>
#include "SDLApplicationDelegate.h"
#include "emulator.h"
#include <sys/param.h> /* for MAXPATHLEN */
#include <unistd.h>
#include "ui/gui.h"
#include "oslib/oslib.h"

#ifdef USE_BREAKPAD
#include "client/mac/handler/exception_handler.h"
#endif

/* For some reaon, Apple removed setAppleMenu from the headers in 10.4,
 but the method still is there and works. To avoid warnings, we declare
 it ourselves here. */
@interface NSApplication(SDL_Missing_Methods)
- (void)setAppleMenu:(NSMenu *)menu;
@end

/* Use this flag to determine whether we use CPS (docking) or not */
#define        SDL_USE_CPS        1
#ifdef SDL_USE_CPS
/* Portions of CPS.h */
typedef struct CPSProcessSerNum
{
    UInt32        lo;
    UInt32        hi;
} CPSProcessSerNum;

extern "C" {
    OSErr CPSGetCurrentProcess( CPSProcessSerNum *psn);
    OSErr CPSEnableForegroundOperation( CPSProcessSerNum *psn, UInt32 _arg2, UInt32 _arg3, UInt32 _arg4, UInt32 _arg5);
    OSErr CPSSetFrontProcess( CPSProcessSerNum *psn);
}
#endif /* SDL_USE_CPS */

static NSString *getApplicationName(void)
{
    const NSDictionary *dict;
    NSString *appName = 0;
    
    /* Determine the application name */
    dict = (const NSDictionary *)CFBundleGetInfoDictionary(CFBundleGetMainBundle());
    if (dict)
        appName = [dict objectForKey: @"CFBundleName"];
    
    if (![appName length])
        appName = [[NSProcessInfo processInfo] processName];
    
    return appName;
}

@interface NSApplication (SDLApplication)
@end

@implementation NSApplication (SDLApplication)
/* Invoked from the Quit menu item */
- (void)quitAction:(id)sender
{
    /* Post a SDL_QUIT event */
    SDL_Event event;
    event.type = SDL_QUIT;
    SDL_PushEvent(&event);
}

- (void)undoAction:(id)sender
{
    gui_keyboard_key(0xE3, true); // Cmd
    gui_keyboard_key(0x1D, true); // Z
    gui_keyboard_key(0x1D, false);
    gui_keyboard_key(0xE3, false);
}

- (void)redoAction:(id)sender
{
    gui_keyboard_key(0xE3, true); // Cmd
    gui_keyboard_key(0xE1, true); // Shift
    gui_keyboard_key(0x1D, true); // Z
    gui_keyboard_key(0x1D, false);
    gui_keyboard_key(0xE1, false);
    gui_keyboard_key(0xE3, false);
}

- (void)cutAction:(id)sender
{
    gui_keyboard_key(0xE3, true); // Cmd
    gui_keyboard_key(0x1B, true); // X
    gui_keyboard_key(0x1B, false);
    gui_keyboard_key(0xE3, false);
}

- (void)copyAction:(id)sender
{
    gui_keyboard_key(0xE3, true); // Cmd
    gui_keyboard_key(0x06, true); // C
    gui_keyboard_key(0x06, false);
    gui_keyboard_key(0xE3, false);
}

- (void)pasteAction:(id)sender
{
    gui_keyboard_key(0xE3, true); // Cmd
    gui_keyboard_key(0x19, true); // V
    gui_keyboard_key(0x19, false);
    gui_keyboard_key(0xE3, false);
}

- (void)selectAllAction:(id)sender
{
    gui_keyboard_key(0xE3, true); // Cmd
    gui_keyboard_key(0x04, true); // A
    gui_keyboard_key(0x04, false);
    gui_keyboard_key(0xE3, false);
}


@end

/* The main class of the application, the application's delegate */
@implementation SDLApplicationDelegate

/* Set the working directory to the .app's parent directory */
- (void) setupWorkingDirectory
{
	if([[NSProcessInfo processInfo] environment][@"PWD"] == NULL && [[[NSFileManager defaultManager] currentDirectoryPath] isEqualToString:@"/"])
    {
		chdir([[[[NSBundle mainBundle] bundlePath] stringByDeletingLastPathComponent] cStringUsingEncoding:NSUTF8StringEncoding]);
    }
}

- (void)newInstance:(id)sender
{
    [NSTask launchedTaskWithLaunchPath:@"/usr/bin/open" arguments:@[@"-n", [[NSBundle mainBundle] bundlePath]]];
}

- (void)toggleMenu:(id)sender
{
    gui_open_settings();
}

static void setApplicationMenu(void)
{
    /* warning: this code is very odd */
    NSMenu *appleMenu;
    NSMenuItem *menuItem;
    NSString *title;
    NSString *appName;
    
    appName = getApplicationName();
    appleMenu = [[NSMenu alloc] initWithTitle:@""];
    
    /* Add menu items */
    title = [@"About " stringByAppendingString:appName];
    [appleMenu addItemWithTitle:title action:@selector(orderFrontStandardAboutPanel:) keyEquivalent:@""];
    
    [appleMenu addItem:[NSMenuItem separatorItem]];
    
    [appleMenu addItemWithTitle:@"New Instance" action:@selector(newInstance:) keyEquivalent:@"n"];

    [appleMenu addItemWithTitle:@"Toggle Menu" action:@selector(toggleMenu:) keyEquivalent:@"m"];

    [appleMenu addItem:[NSMenuItem separatorItem]];
    
    title = [@"Hide " stringByAppendingString:appName];
    [appleMenu addItemWithTitle:title action:@selector(hide:) keyEquivalent:@"h"];
    
    menuItem = (NSMenuItem *)[appleMenu addItemWithTitle:@"Hide Others" action:@selector(hideOtherApplications:) keyEquivalent:@"h"];
    [menuItem setKeyEquivalentModifierMask:(NSAlternateKeyMask|NSCommandKeyMask)];
    
    [appleMenu addItemWithTitle:@"Show All" action:@selector(unhideAllApplications:) keyEquivalent:@""];
    
    [appleMenu addItem:[NSMenuItem separatorItem]];
    
    title = [@"Quit " stringByAppendingString:appName];
    [appleMenu addItemWithTitle:title action:@selector(quitAction:) keyEquivalent:@"q"];
    
    
    /* Put menu into the menubar */
    menuItem = [[NSMenuItem alloc] initWithTitle:@"" action:nil keyEquivalent:@""];
    [menuItem setSubmenu:appleMenu];
    [[NSApp mainMenu] addItem:menuItem];
    
    NSMenuItem *editMenuItem = [[NSMenuItem alloc] initWithTitle:@"" action:nil keyEquivalent:@""];
    NSMenu *editMenu = [[NSMenu alloc] initWithTitle:@"Edit"];
    [editMenu addItemWithTitle:@"Undo" action:@selector(undoAction:) keyEquivalent:@"z"];
    [editMenu addItemWithTitle:@"Redo" action:@selector(redoAction:) keyEquivalent:@"Z"];
    [editMenu addItem:[NSMenuItem separatorItem]];
    [editMenu addItemWithTitle:@"Cut" action:@selector(cutAction:) keyEquivalent:@"x"];
    [editMenu addItemWithTitle:@"Copy" action:@selector(copyAction:) keyEquivalent:@"c"];
    [editMenu addItemWithTitle:@"Paste" action:@selector(pasteAction:) keyEquivalent:@"v"];
    [editMenu addItemWithTitle:@"Select All" action:@selector(selectAllAction:) keyEquivalent:@"a"];
    [editMenuItem setSubmenu:editMenu];
    [[NSApp mainMenu] addItem:editMenuItem];
    
    /* Tell the application object that this is now the application menu */
    [NSApp setAppleMenu:appleMenu];
    
    /* Finally give up our references to the objects */
    [appleMenu release];
    [menuItem release];
    [editMenuItem release];
    [editMenu release];
}

/* Create a window menu */
static void setupWindowMenu(void)
{
    NSMenu      *windowMenu;
    NSMenuItem  *windowMenuItem;
    NSMenuItem  *menuItem;
    
    windowMenu = [[NSMenu alloc] initWithTitle:@"Window"];
    
    /* "Minimize" item */
    menuItem = [[NSMenuItem alloc] initWithTitle:@"Minimize" action:@selector(performMiniaturize:) keyEquivalent:@"m"];
    [windowMenu addItem:menuItem];
    [menuItem release];
    
    /* Put menu into the menubar */
    windowMenuItem = [[NSMenuItem alloc] initWithTitle:@"Window" action:nil keyEquivalent:@""];
    [windowMenuItem setSubmenu:windowMenu];
    [[NSApp mainMenu] addItem:windowMenuItem];
    
    /* Tell the application object that this is now the window menu */
    [NSApp setWindowsMenu:windowMenu];
    
    /* Finally give up our references to the objects */
    [windowMenu release];
    [windowMenuItem release];
}

/* Create a help menu - sample entries */
static void setupHelpMenu(void)
{
    NSMenu      *helpMenu;
    NSMenuItem  *helpMenuItem;
    NSMenuItem  *menuItem;
    
    helpMenu = [[NSMenu alloc] initWithTitle:@"Help"];
    
    /* Standard Apple Help item */
    menuItem = [[NSMenuItem alloc] initWithTitle:@"" action:@selector(showHelp:) keyEquivalent:@"/"];

    /* Put menu into the menubar */
    helpMenuItem = [[NSMenuItem alloc] initWithTitle:@"Help" action:nil keyEquivalent:@""];
    [helpMenuItem setSubmenu:helpMenu];
    [[NSApp mainMenu] addItem:helpMenuItem];
    
    /* Finally give up our references to the objects */
    [helpMenu release];
    [helpMenuItem release];
}
/* end help menu */


/* Replacement for NSApplicationMain */
static void CustomApplicationMain (int argc, char **argv)
{
    NSAutoreleasePool    *pool = [[NSAutoreleasePool alloc] init];
    SDLApplicationDelegate                *appDelegate;

    /* Ensure the application object is initialised */
    [NSApplication sharedApplication];
    
#ifdef SDL_USE_CPS
    {
        CPSProcessSerNum PSN;
        /* Tell the dock about us */
        if (!CPSGetCurrentProcess(&PSN))
            if (!CPSEnableForegroundOperation(&PSN,0x03,0x3C,0x2C,0x1103))
                if (!CPSSetFrontProcess(&PSN))
                    [NSApplication sharedApplication];
    }
#endif /* SDL_USE_CPS */
    
    /* Set up the menubar */
    [NSApp setMainMenu:[[[NSMenu alloc] init] autorelease]];
    setApplicationMenu();
    setupWindowMenu();
    setupHelpMenu(); /* needed for help menu */
    
    /* Create SDLMain and make it the app delegate */
    appDelegate = [[SDLApplicationDelegate alloc] init];
    [NSApp setDelegate:appDelegate];
    
    /* Start the main event loop */
    [NSApp run];
    
    [appDelegate release];
    [pool release];
}


#ifdef USE_BREAKPAD
static bool dumpCallback(const char *dump_dir, const char *minidump_id, void *context, bool succeeded)
{
	if (succeeded)
	{
	    char path[512];
	    sprintf(path, "%s/%s.dmp", dump_dir, minidump_id);
	    printf("Minidump saved to '%s'\n", path);
	    registerCrash(dump_dir, path);
	}
    return succeeded;
}
#endif
/*
 * Catch document open requests...this lets us notice files when the app
 *  was launched by double-clicking a document, or when a document was
 *  dragged/dropped on the app's icon. You need to have a
 *  CFBundleDocumentsType section in your Info.plist to get this message,
 *  apparently.
 *
 * Files are added to gArgv, so to the app, they'll look like command line
 *  arguments. Previously, apps launched from the finder had nothing but
 *  an argv[0].
 *
 * This message may be received multiple times to open several docs on launch.
 *
 * This message is ignored once the app's mainline has been called.
 */
- (BOOL)application:(NSApplication *)theApplication openFile:(NSString *)filename
{
	dispatch_async(dispatch_get_main_queue(), ^(){
		gui_start_game([filename cStringUsingEncoding:NSUTF8StringEncoding]);
	});

    return TRUE;
}


/* Called when the internal event loop has just started running */
- (void) applicationDidFinishLaunching: (NSNotification *) note
{
#ifdef USE_BREAKPAD
    google_breakpad::ExceptionHandler eh("/tmp", NULL, dumpCallback, NULL, true, NULL);
    task_set_exception_ports(mach_task_self(), EXC_MASK_BAD_ACCESS, MACH_PORT_NULL, EXCEPTION_DEFAULT, 0);
#endif
    
    int status;
    
    /* Set the working directory to the .app's parent directory */
    [self setupWorkingDirectory];
    
    status = SDL_main(NULL, NULL);
    
    /* We're done, thank you for playing */
    exit(status);
}

- (NSMenu *)applicationDockMenu:(NSApplication *)sender
{
    NSMenu* menu = [[NSMenu alloc] init];
    [menu addItemWithTitle:@"New Instance" action:@selector(newInstance:) keyEquivalent:@"n"];
    return menu;
}

// Handle Dock menu's Quit action
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)sender
{
    [[NSApplication sharedApplication] quitAction:sender];
    return NSTerminateNow;
}
@end


@implementation NSString (ReplaceSubString)

- (NSString *)stringByReplacingRange:(NSRange)aRange with:(NSString *)aString
{
    unsigned int bufferSize;
    unsigned int selfLen = [self length];
    unsigned int aStringLen = [aString length];
    unichar *buffer;
    NSRange localRange;
    NSString *result;
    
    bufferSize = selfLen + aStringLen - aRange.length;
    buffer = (unichar *)NSAllocateMemoryPages(bufferSize*sizeof(unichar));
    
    /* Get first part into buffer */
    localRange.location = 0;
    localRange.length = aRange.location;
    [self getCharacters:buffer range:localRange];
    
    /* Get middle part into buffer */
    localRange.location = 0;
    localRange.length = aStringLen;
    [aString getCharacters:(buffer+aRange.location) range:localRange];
    
    /* Get last part into buffer */
    localRange.location = aRange.location + aRange.length;
    localRange.length = selfLen - localRange.location;
    [self getCharacters:(buffer+aRange.location+aStringLen) range:localRange];
    
    /* Build output string */
    result = [NSString stringWithCharacters:buffer length:bufferSize];
    
    NSDeallocateMemoryPages(buffer, bufferSize);
    
    return result;
}

@end



#ifdef main
#  undef main
#endif


/* Main entry point to executable - should *not* be SDL_main! */
int main (int argc, char **argv)
{
    if (getppid() != 1) {
        /* Make LLDB ignore EXC_BAD_ACCESS for debugging */
        task_set_exception_ports(mach_task_self(), EXC_MASK_BAD_ACCESS, MACH_PORT_NULL, EXCEPTION_DEFAULT, 0);
    }
    
    CustomApplicationMain (argc, argv);
    return 0;
}

