/*
MACOS_DISCORD.C

The Mac app as the handler of discord-<application> links, how Discord
starts the game for a friend's Join when it is closed (posix_net.c's
posix_register_discord_launch, port/linux/src/discord_register.h): the
bundle declares the scheme (port/macos/Info.plist), and Launch Services is
told the app is its handler, as Discord's SDK does for a bundle
(discord-rpc's discord_register_osx.m, without a command). A handler of
another program's is left as it is.
*/

#include <CoreFoundation/CoreFoundation.h>
#include <CoreServices/CoreServices.h>
#include <stdio.h>

#include "../../linux/src/discord_register.h"

int macos_register_discord_scheme(const char *application)
{
	char scheme[48];
	CFBundleRef bundle = CFBundleGetMainBundle();
	CFStringRef identifier = bundle ? CFBundleGetIdentifier(bundle) : NULL;
	CFStringRef scheme_string, handler;
	int result = DISCORD_REGISTER_LEFT;

	/* (the game run outside its bundle, as the tests run it: none) */
	if (!identifier || !discord_register_application_valid(application))
		return DISCORD_REGISTER_NONE;
	snprintf(scheme, sizeof(scheme), "discord-%s", application);
	scheme_string = CFStringCreateWithCString(NULL, scheme, kCFStringEncodingUTF8);
	if (!scheme_string)
		return DISCORD_REGISTER_FAILED;
	/* (deprecated, without a replacement for a scheme in C; the SDK's call) */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
	handler = LSCopyDefaultHandlerForURLScheme(scheme_string);
	if (handler && CFStringCompare(handler, identifier, kCFCompareCaseInsensitive) == kCFCompareEqualTo)
		result = DISCORD_REGISTER_UNCHANGED;
	/* (none yet, or this game's under a bundle identifier of before) */
	else if (!handler || CFStringHasPrefix(handler, CFSTR("io.github.chupathingyce")) ||
		CFStringHasPrefix(handler, CFSTR("io.github.mrmilenko.halo-ce")))
	{
		result = LSSetDefaultHandlerForURLScheme(scheme_string, identifier) == noErr ? DISCORD_REGISTER_WRITTEN :
			DISCORD_REGISTER_FAILED;
	}
#pragma clang diagnostic pop
	if (handler)
		CFRelease(handler);
	CFRelease(scheme_string);
	return result;
}
