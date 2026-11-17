/*
BGFX_VULKAN_CHECK.INL

Before bgfx starts on Vulkan (Windows, Linux; FreeBSD once it has bgfx): a look at the
computer's Vulkan, so that a machine whose Vulkan is missing, broken or
known to be slow draws with OpenGL instead (platform_video_reopen_for_gl).
Old laptops on new systems are the usual case: nouveau's Vulkan (NVK) on a
GeForce from before Turing, NVIDIA's legacy drivers, Mesa's Vulkan for
Haswell and older Intel graphics, or no Vulkan driver at all.

The loader is opened by hand (SDL_LoadObject), an instance made, and each
GPU's name, driver and IDs read. A GPU that a rule in bg_vulkan_blocklist
names is passed over, and bgfx is told which of the others to use (its
vendor and device IDs); with none left, bgfx is not started. Each GPU gets a
line in the log, with why it was passed over.

display.vulkan_blocklist = false (HALO_VULKAN_BLOCKLIST=false) turns the
rules off, for testing a driver they name (and for the picture checks,
which run on Mesa's software Vulkan). Part of d3d8_bgfx.inl.
*/

#define VK_NO_PROTOTYPES
#include <vulkan-local/vulkan_core.h>

#ifdef _WIN32
/* win32_posix.c's: a DLL from System32 only, never the game's folder or the
working directory (an SDL_SharedObject, as SDL_LoadObject's are: a module) */
void *win32_system_library(const char *name);
#endif

/* the rules, one a line: words of key=value, all of which must match a GPU
for it to be passed over, then "#" and the reason the log gives.

	driver=NAME      the driver (VkDriverId): nvk, nvidia, radv, amd,
	                 amdvlk, anv (Mesa's Intel, hasvk included), intel
	                 (Intel's Windows driver), lavapipe, venus, turnip,
	                 dozen, moltenvk, honeykrisp, v3dv, panvk; unknown when
	                 the driver does not say, other for one not listed here
	vendor=HEX       the PCI vendor (10de NVIDIA, 1002 AMD, 8086 Intel)
	device_below=HEX the PCI device ID is below this
	version_below=N  the driver's version is below N (NVIDIA's own numbers,
	                 such as 470; others major.minor, such as 23.1)
	name=TEXT        the GPU's name has TEXT in it (no spaces)
	type=cpu         a software renderer

A line is added here when testers show a driver that starts but draws
wrong or slowly; OpenGL is the fallback for all of them. */
static const char bg_vulkan_blocklist[] =
	/* Mesa's software Vulkan: OpenGL, even a software one, is faster */
	"type=cpu # a software renderer\n"
	/* nouveau's Vulkan before Turing: no reclocking, a slide show */
	"driver=nvk vendor=10de device_below=1e00 # nouveau's Vulkan on a GeForce older than Turing\n"
	/* NVIDIA's legacy branches (340 has no Vulkan, 390 and 418 are old and
	end of life) */
	"driver=nvidia version_below=470 # an NVIDIA driver older than 470\n"
	/* (those branches often can't name their driver, VK_KHR_driver_properties
	being newer: an NVIDIA GPU with no driver named and NVIDIA's numbering.
	NVK always names itself, so this never catches it) */
	"driver=unknown vendor=10de version_below=470 # an NVIDIA driver older than 470\n"
	/* Mesa's Vulkan for Ivy Bridge, Haswell and Bay Trail (hasvk): not
	conformant */
	"driver=anv name=(IVB # Mesa's Vulkan for Ivy Bridge graphics\n"
	"driver=anv name=(HSW # Mesa's Vulkan for Haswell graphics\n"
	"driver=anv name=(BYT # Mesa's Vulkan for Bay Trail graphics\n";

static const struct
{
	const char *name;
	VkDriverId id;
} bg_vulkan_drivers[] =
{
	{ "amd", VK_DRIVER_ID_AMD_PROPRIETARY },
	{ "amdvlk", VK_DRIVER_ID_AMD_OPEN_SOURCE },
	{ "radv", VK_DRIVER_ID_MESA_RADV },
	{ "nvidia", VK_DRIVER_ID_NVIDIA_PROPRIETARY },
	{ "intel", VK_DRIVER_ID_INTEL_PROPRIETARY_WINDOWS },
	{ "anv", VK_DRIVER_ID_INTEL_OPEN_SOURCE_MESA },
	{ "lavapipe", VK_DRIVER_ID_MESA_LLVMPIPE },
	{ "venus", VK_DRIVER_ID_MESA_VENUS },
	{ "turnip", VK_DRIVER_ID_MESA_TURNIP },
	{ "dozen", VK_DRIVER_ID_MESA_DOZEN },
	{ "moltenvk", VK_DRIVER_ID_MOLTENVK },
	{ "nvk", VK_DRIVER_ID_MESA_NVK },
	{ "honeykrisp", VK_DRIVER_ID_MESA_HONEYKRISP },
	{ "v3dv", VK_DRIVER_ID_MESA_V3DV },
	{ "panvk", VK_DRIVER_ID_MESA_PANVK },
};

struct bg_vulkan_gpu
{
	char name[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE];
	const char *driver;
	/* (0 when the driver does not say) */
	VkDriverId driver_id;
	char driver_info[VK_MAX_DRIVER_INFO_SIZE];
	unsigned vendor, device;
	uint32_t version;
	VkPhysicalDeviceType type;
};

static const char *bg_vulkan_driver_name(VkDriverId id)
{
	size_t index;

	for (index = 0; index < sizeof(bg_vulkan_drivers) / sizeof(bg_vulkan_drivers[0]); index++)
	{
		if (bg_vulkan_drivers[index].id == id)
			return bg_vulkan_drivers[index].name;
	}
	return "other";
}

/* the driver's version as it numbers it: NVIDIA's 10.8.8.6 bits (major
in the top ten), the others Vulkan's 10.10.12; major * 1000 + minor */
static unsigned long bg_vulkan_driver_version(const struct bg_vulkan_gpu *gpu)
{
	if (gpu->driver_id == VK_DRIVER_ID_NVIDIA_PROPRIETARY || (!gpu->driver_id && gpu->vendor == 0x10de))
		return (gpu->version >> 22) * 1000ul + ((gpu->version >> 14) & 0xff);
	return VK_API_VERSION_MAJOR(gpu->version) * 1000ul + VK_API_VERSION_MINOR(gpu->version);
}

/* a rule's version: "470" (470000) or "23.1" (23001) */
static unsigned long bg_vulkan_rule_version(const char *text)
{
	char *end;
	unsigned long major = strtoul(text, &end, 10), minor = 0;

	if (*end == '.')
		minor = strtoul(end + 1, NULL, 10);
	return major * 1000ul + minor;
}

/* whether the rule (a line of bg_vulkan_blocklist, without its end) names
the GPU; a key the rule has that is not known never matches */
static BOOL bg_vulkan_rule_matches(const char *rule, size_t length, const struct bg_vulkan_gpu *gpu)
{
	const char *word = rule, *end = rule + length;
	BOOL any = FALSE;

	while (word < end)
	{
		char key[32], value[96];
		const char *stop, *equals;

		while (word < end && (*word == ' ' || *word == '\t'))
			word++;
		if (word >= end)
			break;
		for (stop = word; stop < end && *stop != ' ' && *stop != '\t'; stop++)
			;
		equals = memchr(word, '=', (size_t)(stop - word));
		if (!equals || (size_t)(equals - word) >= sizeof(key) || (size_t)(stop - equals - 1) >= sizeof(value))
			return FALSE;
		memcpy(key, word, (size_t)(equals - word));
		key[equals - word] = 0;
		memcpy(value, equals + 1, (size_t)(stop - equals - 1));
		value[stop - equals - 1] = 0;
		word = stop;
		any = TRUE;
		if (!strcmp(key, "driver"))
		{
			if (strcmp(value, gpu->driver))
				return FALSE;
		}
		else if (!strcmp(key, "vendor"))
		{
			if (strtoul(value, NULL, 16) != gpu->vendor)
				return FALSE;
		}
		else if (!strcmp(key, "device_below"))
		{
			if (gpu->device >= strtoul(value, NULL, 16))
				return FALSE;
		}
		else if (!strcmp(key, "version_below"))
		{
			if (bg_vulkan_driver_version(gpu) >= bg_vulkan_rule_version(value))
				return FALSE;
		}
		else if (!strcmp(key, "name"))
		{
			if (!strstr(gpu->name, value))
				return FALSE;
		}
		else if (!strcmp(key, "type"))
		{
			if (strcmp(value, "cpu") || gpu->type != VK_PHYSICAL_DEVICE_TYPE_CPU)
				return FALSE;
		}
		else
			return FALSE;
	}
	return any;
}

/* the reason a rule passes the GPU over, written to reason; NULL for none */
static const char *bg_vulkan_blocked(const struct bg_vulkan_gpu *gpu, char *reason, size_t size)
{
	const char *line, *end;

	for (line = bg_vulkan_blocklist; *line; line = *end ? end + 1 : end)
	{
		const char *hash;
		size_t rule_length;

		end = strchr(line, '\n');
		if (!end)
			end = line + strlen(line);
		hash = memchr(line, '#', (size_t)(end - line));
		rule_length = (size_t)((hash ? hash : end) - line);
		while (rule_length && (line[rule_length - 1] == ' ' || line[rule_length - 1] == '\t'))
			rule_length--;
		if (rule_length && bg_vulkan_rule_matches(line, rule_length, gpu))
		{
			const char *why = hash ? hash + 1 : line;
			size_t why_length = (size_t)(end - why);

			while (why_length && *why == ' ')
				why++, why_length--;
			snprintf(reason, size, "%.*s", (int)why_length, why);
			return reason;
		}
	}
	return NULL;
}

/* bgfx's order among the GPUs: discrete, integrated, virtual, others */
static int bg_vulkan_rank(VkPhysicalDeviceType type)
{
	switch (type)
	{
	case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return 0;
	case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return 1;
	case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: return 2;
	default: return 3;
	}
}

/* whether bgfx may start on Vulkan: a GPU left after the rules, whose IDs
go to *vendor and *device (bgfx_init_t's, so that bgfx takes that one) */
static BOOL bg_vulkan_check(uint16_t *vendor, uint16_t *device)
{
#if defined(_WIN32)
	static const char *const loaders[] = { "vulkan-1.dll", NULL };
#elif defined(__APPLE__)
	static const char *const loaders[] = { "libvulkan.1.dylib", NULL };
#else
	static const char *const loaders[] = { "libvulkan.so.1", "libvulkan.so", NULL };
#endif
	SDL_SharedObject *library = NULL;
	PFN_vkGetInstanceProcAddr get_instance_proc;
	PFN_vkCreateInstance create_instance;
	PFN_vkEnumerateInstanceVersion instance_version;
	PFN_vkDestroyInstance destroy_instance;
	PFN_vkEnumeratePhysicalDevices enumerate_devices;
	PFN_vkGetPhysicalDeviceProperties device_properties;
	PFN_vkGetPhysicalDeviceProperties2 device_properties2 = NULL;
	PFN_vkEnumerateDeviceExtensionProperties device_extensions;
	VkApplicationInfo application = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
	VkInstanceCreateInfo create = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
	VkInstance instance = VK_NULL_HANDLE;
	VkPhysicalDevice devices[16];
	uint32_t count = 16, api = VK_API_VERSION_1_0, index;
	BOOL rules = config_boolean("display.vulkan_blocklist"), found = FALSE;
	int best_rank = 99;
	VkResult result;

	for (index = 0; loaders[index] && !library; index++)
#ifdef _WIN32
		library = (SDL_SharedObject *)win32_system_library(loaders[index]);
#else
		library = SDL_LoadObject(loaders[index]);
#endif
	if (!library)
	{
		platform_log("bgfx: no Vulkan driver is installed (%s not found)", loaders[0]);
		return FALSE;
	}
	get_instance_proc = (PFN_vkGetInstanceProcAddr)SDL_LoadFunction(library, "vkGetInstanceProcAddr");
	create_instance = get_instance_proc ? (PFN_vkCreateInstance)get_instance_proc(NULL, "vkCreateInstance") : NULL;
	if (!create_instance)
	{
		platform_log("bgfx: the Vulkan loader is broken (no vkCreateInstance)");
		SDL_UnloadObject(library);
		return FALSE;
	}
	/* (1.1 where the loader has it: the drivers' names come through
	vkGetPhysicalDeviceProperties2) */
	instance_version = (PFN_vkEnumerateInstanceVersion)get_instance_proc(NULL, "vkEnumerateInstanceVersion");
	if (instance_version && instance_version(&api) != VK_SUCCESS)
		api = VK_API_VERSION_1_0;
	application.pApplicationName = "Chupathingy";
	application.apiVersion = api >= VK_API_VERSION_1_1 ? VK_API_VERSION_1_1 : VK_API_VERSION_1_0;
	create.pApplicationInfo = &application;
	result = create_instance(&create, NULL, &instance);
	if (result != VK_SUCCESS)
	{
		platform_log("bgfx: Vulkan does not start (vkCreateInstance: %d)", (int)result);
		SDL_UnloadObject(library);
		return FALSE;
	}
	destroy_instance = (PFN_vkDestroyInstance)get_instance_proc(instance, "vkDestroyInstance");
	enumerate_devices = (PFN_vkEnumeratePhysicalDevices)get_instance_proc(instance, "vkEnumeratePhysicalDevices");
	device_properties = (PFN_vkGetPhysicalDeviceProperties)get_instance_proc(instance,
		"vkGetPhysicalDeviceProperties");
	device_extensions = (PFN_vkEnumerateDeviceExtensionProperties)get_instance_proc(instance,
		"vkEnumerateDeviceExtensionProperties");
	if (application.apiVersion >= VK_API_VERSION_1_1)
		device_properties2 = (PFN_vkGetPhysicalDeviceProperties2)get_instance_proc(instance,
			"vkGetPhysicalDeviceProperties2");
	if (!destroy_instance || !enumerate_devices || !device_properties || !device_extensions ||
		enumerate_devices(instance, &count, devices) < 0)
		count = 0;
	if (!count)
		platform_log("bgfx: Vulkan has no GPU (no driver for this one is installed)");
	for (index = 0; index < count; index++)
	{
		VkPhysicalDeviceProperties properties;
		struct bg_vulkan_gpu gpu;
		char reason[160];
		const char *blocked;

		memset(&gpu, 0, sizeof(gpu));
		device_properties(devices[index], &properties);
		/* the driver, where it says (Vulkan 1.2, or VK_KHR_driver_properties) */
		if (device_properties2)
		{
			BOOL has_driver = properties.apiVersion >= VK_API_VERSION_1_2;
			VkExtensionProperties extensions[256];
			uint32_t extension_count = 256, extension;

			if (!has_driver && device_extensions(devices[index], NULL, &extension_count, extensions) >= 0)
			{
				for (extension = 0; extension < extension_count && !has_driver; extension++)
					has_driver = !strcmp(extensions[extension].extensionName, VK_KHR_DRIVER_PROPERTIES_EXTENSION_NAME);
			}
			if (has_driver)
			{
				VkPhysicalDeviceDriverProperties driver = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES };
				VkPhysicalDeviceProperties2 properties2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };

				properties2.pNext = &driver;
				device_properties2(devices[index], &properties2);
				gpu.driver_id = driver.driverID;
				snprintf(gpu.driver_info, sizeof(gpu.driver_info), "%s", driver.driverInfo);
			}
		}
		snprintf(gpu.name, sizeof(gpu.name), "%s", properties.deviceName);
		gpu.driver = gpu.driver_id ? bg_vulkan_driver_name(gpu.driver_id) : "unknown";
		gpu.vendor = properties.vendorID;
		gpu.device = properties.deviceID;
		gpu.version = properties.driverVersion;
		gpu.type = properties.deviceType;
		blocked = rules ? bg_vulkan_blocked(&gpu, reason, sizeof(reason)) : NULL;
		platform_log("bgfx: Vulkan GPU %u: %s (driver %s%s%s, vendor %04x, device %04x)%s%s", (unsigned)index,
			gpu.name, gpu.driver, *gpu.driver_info ? " " : "", gpu.driver_info, gpu.vendor, gpu.device,
			blocked ? ": passed over, " : "", blocked ? blocked : "");
		if (!blocked && bg_vulkan_rank(gpu.type) < best_rank)
		{
			best_rank = bg_vulkan_rank(gpu.type);
			/* (bgfx's IDs are 16 bits: a Khronos vendor ID such as Mesa's
			0x10005 lets bgfx choose, as it would) */
			*vendor = gpu.vendor <= 0xffff ? (uint16_t)gpu.vendor : 0;
			*device = gpu.vendor <= 0xffff ? (uint16_t)gpu.device : 0;
			found = TRUE;
		}
	}
	if (count && !found)
		platform_log("bgfx: no GPU is left for Vulkan (display.vulkan_blocklist = false tries anyway)");
	if (destroy_instance)
		destroy_instance(instance, NULL);
	SDL_UnloadObject(library);
	return found;
}
