#define __STDC_LIMIT_MACROS
#include <cstdlib>
#include <cstdint>
#include <assert.h>
#include <cstring>
#include <iostream>

#include "base/basictypes.h"
#include "base/display.h"
#include "VulkanContext.h"
#include "GPU/Common/ShaderCommon.h"
#include "Common/StringUtils.h"
#include "Core/Config.h"

// Change this to 1, 2, and 3 to fake failures in a few places, so that
// we can test our fallback-to-GL code.
#define SIMULATE_VULKAN_FAILURE 0

#ifdef USE_CRT_DBG
#undef new
#endif

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:4996)
#endif

#include "ext/glslang/SPIRV/GlslangToSpv.h"

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#ifdef USE_CRT_DBG
#define new DBG_NEW
#endif

static const char *validationLayers[] = {
	"VK_LAYER_LUNARG_standard_validation",
	/*
	"VK_LAYER_GOOGLE_threading",
	"VK_LAYER_LUNARG_draw_state",
	"VK_LAYER_LUNARG_image",
	"VK_LAYER_LUNARG_mem_tracker",
	"VK_LAYER_LUNARG_object_tracker",
	"VK_LAYER_LUNARG_param_checker",
	*/
	/*
	// For layers included in the Android NDK.
	"VK_LAYER_GOOGLE_threading",
	"VK_LAYER_LUNARG_parameter_validation",
	"VK_LAYER_LUNARG_core_validation",
	"VK_LAYER_LUNARG_image",
	"VK_LAYER_LUNARG_object_tracker",
	"VK_LAYER_LUNARG_swapchain",
	"VK_LAYER_GOOGLE_unique_objects",
	*/
};

std::string VulkanVendorString(uint32_t vendorId) {
	switch (vendorId) {
	case VULKAN_VENDOR_INTEL: return "Intel";
	case VULKAN_VENDOR_NVIDIA: return "nVidia";
	case VULKAN_VENDOR_AMD: return "AMD";
	case VULKAN_VENDOR_ARM: return "ARM";
	case VULKAN_VENDOR_QUALCOMM: return "Qualcomm";
	case VULKAN_VENDOR_IMGTEC: return "Imagination";

	default:
		return StringFromFormat("%08x", vendorId);
	}
}

const char *PresentModeString(VkPresentModeKHR presentMode) {
	switch (presentMode) {
	case VK_PRESENT_MODE_IMMEDIATE_KHR: return "IMMEDIATE";
	case VK_PRESENT_MODE_MAILBOX_KHR: return "MAILBOX";
	case VK_PRESENT_MODE_FIFO_KHR: return "FIFO";
	case VK_PRESENT_MODE_FIFO_RELAXED_KHR: return "FIFO_RELAXED";
	default: return "UNKNOWN";
	}
}

VulkanContext::VulkanContext() {
#if SIMULATE_VULKAN_FAILURE == 1
	return;
#endif
	if (!VulkanLoad()) {
		init_error_ = "Failed to load Vulkan driver library";
		// No DLL?
		return;
	}

	// We can get the list of layers and extensions without an instance so we can use this information
	// to enable the extensions we need that are available.
	GetInstanceLayerProperties();
	GetInstanceLayerExtensionList(nullptr, instance_extension_properties_);
}

VkResult VulkanContext::CreateInstance(const CreateInfo &info) {
	if (!vkCreateInstance) {
		init_error_ = "Vulkan not loaded - can't create instance";
		return VK_ERROR_INITIALIZATION_FAILED;
	}

	flags_ = info.flags;

	// List extensions to try to enable.
	instance_extensions_enabled_.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
#ifdef _WIN32
	instance_extensions_enabled_.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
#elif defined(__ANDROID__)
	instance_extensions_enabled_.push_back(VK_KHR_ANDROID_SURFACE_EXTENSION_NAME);
#else
#if defined(VK_USE_PLATFORM_XLIB_KHR)
	if (IsInstanceExtensionAvailable(VK_KHR_XLIB_SURFACE_EXTENSION_NAME)) {
		instance_extensions_enabled_.push_back(VK_KHR_XLIB_SURFACE_EXTENSION_NAME);
	}
#endif
//#if defined(VK_USE_PLATFORM_XCB_KHR)
//	instance_extensions_enabled_.push_back(VK_KHR_XCB_SURFACE_EXTENSION_NAME);
//#endif
//#if defined(VK_USE_PLATFORM_MIR_KHR)
//	instance_extensions_enabled_.push_back(VK_KHR_MIR_SURFACE_EXTENSION_NAME);
//#endif
#if defined(VK_USE_PLATFORM_WAYLAND_KHR)
	if (IsInstanceExtensionAvailable(VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME)) {
		instance_extensions_enabled_.push_back(VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME);
	}
#endif
#endif

	if (flags_ & VULKAN_FLAG_VALIDATE) {
		if (IsInstanceExtensionAvailable(VK_EXT_DEBUG_REPORT_EXTENSION_NAME)) {
			for (size_t i = 0; i < ARRAY_SIZE(validationLayers); i++) {
				instance_layer_names_.push_back(validationLayers[i]);
				device_layer_names_.push_back(validationLayers[i]);
			}
			instance_extensions_enabled_.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
		} else {
			ELOG("Validation layer extension not available - not enabling Vulkan validation.");
			flags_ &= ~VULKAN_FLAG_VALIDATE;
		}
	}

	// Validate that all the instance extensions we ask for are actually available.
	for (auto ext : instance_extensions_enabled_) {
		if (!IsInstanceExtensionAvailable(ext))
			WLOG("WARNING: Does not seem that instance extension '%s' is available. Trying to proceed anyway.", ext);
	}

	VkApplicationInfo app_info{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
	app_info.pApplicationName = info.app_name;
	app_info.applicationVersion = info.app_ver;
	app_info.pEngineName = info.app_name;
	// Let's increment this when we make major engine/context changes.
	app_info.engineVersion = 2;
	app_info.apiVersion = VK_API_VERSION_1_0;

	VkInstanceCreateInfo inst_info{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
	inst_info.flags = 0;
	inst_info.pApplicationInfo = &app_info;
	inst_info.enabledLayerCount = (uint32_t)instance_layer_names_.size();
	inst_info.ppEnabledLayerNames = instance_layer_names_.size() ? instance_layer_names_.data() : nullptr;
	inst_info.enabledExtensionCount = (uint32_t)instance_extensions_enabled_.size();
	inst_info.ppEnabledExtensionNames = instance_extensions_enabled_.size() ? instance_extensions_enabled_.data() : nullptr;

#if SIMULATE_VULKAN_FAILURE == 2
	VkResult res = VK_ERROR_INCOMPATIBLE_DRIVER;
#else
	VkResult res = vkCreateInstance(&inst_info, nullptr, &instance_);
#endif
	if (res != VK_SUCCESS) {
		if (res == VK_ERROR_LAYER_NOT_PRESENT) {
			WLOG("Validation on but layers not available - dropping layers");
			// Drop the validation layers and try again.
			instance_layer_names_.clear();
			device_layer_names_.clear();
			inst_info.enabledLayerCount = 0;
			inst_info.ppEnabledLayerNames = nullptr;
			res = vkCreateInstance(&inst_info, nullptr, &instance_);
			if (res != VK_SUCCESS)
				ELOG("Failed to create instance even without validation: %d", res);
		} else {
			ELOG("Failed to create instance : %d", res);
		}
	}
	if (res != VK_SUCCESS) {
		init_error_ = "Failed to create Vulkan instance";
		return res;
	}

	VulkanLoadInstanceFunctions(instance_);
	if (!CheckLayers(instance_layer_properties_, instance_layer_names_)) {
		WLOG("CheckLayers for instance failed");
		// init_error_ = "Failed to validate instance layers";
		// return;
	}

	uint32_t gpu_count = 1;
#if SIMULATE_VULKAN_FAILURE == 3
	gpu_count = 0;
#else
	res = vkEnumeratePhysicalDevices(instance_, &gpu_count, nullptr);
#endif
	if (gpu_count <= 0) {
		ELOG("Vulkan driver found but no supported GPU is available");
		init_error_ = "No Vulkan physical devices found";
		vkDestroyInstance(instance_, nullptr);
		instance_ = nullptr;
		return VK_ERROR_INITIALIZATION_FAILED;
	}

	assert(gpu_count > 0);
	physical_devices_.resize(gpu_count);
	physicalDeviceProperties_.resize(gpu_count);
	res = vkEnumeratePhysicalDevices(instance_, &gpu_count, physical_devices_.data());
	if (res != VK_SUCCESS) {
		init_error_ = "Failed to enumerate physical devices";
		vkDestroyInstance(instance_, nullptr);
		instance_ = nullptr;
		return res;
	}

	for (uint32_t i = 0; i < gpu_count; i++) {
		vkGetPhysicalDeviceProperties(physical_devices_[i], &physicalDeviceProperties_[i]);
	}
	return VK_SUCCESS;
}

VulkanContext::~VulkanContext() {
	assert(instance_ == VK_NULL_HANDLE);
}

void VulkanContext::DestroyInstance() {
	vkDestroyInstance(instance_, nullptr);
	VulkanFree();
	instance_ = VK_NULL_HANDLE;
}

void VulkanContext::BeginFrame() {
	FrameData *frame = &frame_[curFrame_];
	// Process pending deletes.
	frame->deleteList.PerformDeletes(device_);
}

void VulkanContext::EndFrame() {
	frame_[curFrame_].deleteList.Take(globalDeleteList_);
	curFrame_++;
	if (curFrame_ >= inflightFrames_) {
		curFrame_ = 0;
	}
}

void VulkanContext::WaitUntilQueueIdle() {
	// Should almost never be used
	vkQueueWaitIdle(gfx_queue_);
}

bool VulkanContext::MemoryTypeFromProperties(uint32_t typeBits, VkFlags requirements_mask, uint32_t *typeIndex) {
	// Search memtypes to find first index with those properties
	for (uint32_t i = 0; i < 32; i++) {
		if ((typeBits & 1) == 1) {
			// Type is available, does it match user properties?
			if ((memory_properties.memoryTypes[i].propertyFlags & requirements_mask) == requirements_mask) {
				*typeIndex = i;
				return true;
			}
		}
		typeBits >>= 1;
	}
	// No memory types matched, return failure
	return false;
}

bool VulkanContext::InitObjects() {
	if (!InitQueue()) {
		return false;
	}

	if (!InitSwapchain()) {
		// Destroy queue?
		return false;
	}
	return true;
}

void VulkanContext::DestroyObjects() {
	ILOG("VulkanContext::DestroyObjects (including swapchain)");
	if (swapchain_ != VK_NULL_HANDLE)
		vkDestroySwapchainKHR(device_, swapchain_, nullptr);
	swapchain_ = VK_NULL_HANDLE;

	vkDestroySurfaceKHR(instance_, surface_, nullptr);
	surface_ = VK_NULL_HANDLE;
}

VkResult VulkanContext::GetInstanceLayerExtensionList(const char *layerName, std::vector<VkExtensionProperties> &extensions) {
	VkResult res;
	do {
		uint32_t instance_extension_count;
		res = vkEnumerateInstanceExtensionProperties(layerName, &instance_extension_count, nullptr);
		if (res != VK_SUCCESS)
			return res;
		if (instance_extension_count == 0)
			return VK_SUCCESS;
		extensions.resize(instance_extension_count);
		res = vkEnumerateInstanceExtensionProperties(layerName, &instance_extension_count, extensions.data());
	} while (res == VK_INCOMPLETE);
	return res;
}

VkResult VulkanContext::GetInstanceLayerProperties() {
	/*
	 * It's possible, though very rare, that the number of
	 * instance layers could change. For example, installing something
	 * could include new layers that the loader would pick up
	 * between the initial query for the count and the
	 * request for VkLayerProperties. The loader indicates that
	 * by returning a VK_INCOMPLETE status and will update the
	 * the count parameter.
	 * The count parameter will be updated with the number of
	 * entries loaded into the data pointer - in case the number
	 * of layers went down or is smaller than the size given.
	 */
	uint32_t instance_layer_count;
	std::vector<VkLayerProperties> vk_props;
	VkResult res;
	do {
		res = vkEnumerateInstanceLayerProperties(&instance_layer_count, nullptr);
		if (res != VK_SUCCESS)
			return res;
		if (!instance_layer_count)
			return VK_SUCCESS;
		vk_props.resize(instance_layer_count);
		res = vkEnumerateInstanceLayerProperties(&instance_layer_count, vk_props.data());
	} while (res == VK_INCOMPLETE);

	// Now gather the extension list for each instance layer.
	for (uint32_t i = 0; i < instance_layer_count; i++) {
		LayerProperties layer_props;
		layer_props.properties = vk_props[i];
		res = GetInstanceLayerExtensionList(layer_props.properties.layerName, layer_props.extensions);
		if (res != VK_SUCCESS)
			return res;
		instance_layer_properties_.push_back(layer_props);
	}
	return res;
}

// Pass layerName == nullptr to get the extension list for the device.
VkResult VulkanContext::GetDeviceLayerExtensionList(const char *layerName, std::vector<VkExtensionProperties> &extensions) {
	VkResult res;
	do {
		uint32_t device_extension_count;
		res = vkEnumerateDeviceExtensionProperties(physical_devices_[physical_device_], layerName, &device_extension_count, nullptr);
		if (res != VK_SUCCESS)
			return res;
		if (!device_extension_count)
			return VK_SUCCESS;
		extensions.resize(device_extension_count);
		res = vkEnumerateDeviceExtensionProperties(physical_devices_[physical_device_], layerName, &device_extension_count, extensions.data());
	} while (res == VK_INCOMPLETE);
	return res;
}

VkResult VulkanContext::GetDeviceLayerProperties() {
	/*
	 * It's possible, though very rare, that the number of
	 * instance layers could change. For example, installing something
	 * could include new layers that the loader would pick up
	 * between the initial query for the count and the
	 * request for VkLayerProperties. The loader indicates that
	 * by returning a VK_INCOMPLETE status and will update the
	 * the count parameter.
	 * The count parameter will be updated with the number of
	 * entries loaded into the data pointer - in case the number
	 * of layers went down or is smaller than the size given.
	 */
	uint32_t device_layer_count;
	std::vector<VkLayerProperties> vk_props;
	VkResult res;
	do {
		res = vkEnumerateDeviceLayerProperties(physical_devices_[physical_device_], &device_layer_count, nullptr);
		if (res != VK_SUCCESS)
			return res;
		if (device_layer_count == 0)
			return VK_SUCCESS;
		vk_props.resize(device_layer_count);
		res = vkEnumerateDeviceLayerProperties(physical_devices_[physical_device_], &device_layer_count, vk_props.data());
	} while (res == VK_INCOMPLETE);

	// Gather the list of extensions for each device layer.
	for (uint32_t i = 0; i < device_layer_count; i++) {
		LayerProperties layer_props;
		layer_props.properties = vk_props[i];
		res = GetDeviceLayerExtensionList(layer_props.properties.layerName, layer_props.extensions);
		if (res != VK_SUCCESS)
			return res;
		device_layer_properties_.push_back(layer_props);
	}
	return res;
}

// Returns true if all layer names specified in check_names can be found in given layer properties.
bool VulkanContext::CheckLayers(const std::vector<LayerProperties> &layer_props, const std::vector<const char *> &layer_names) const {
	uint32_t check_count = (uint32_t)layer_names.size();
	uint32_t layer_count = (uint32_t)layer_props.size();
	for (uint32_t i = 0; i < check_count; i++) {
		bool found = false;
		for (uint32_t j = 0; j < layer_count; j++) {
			if (!strcmp(layer_names[i], layer_props[j].properties.layerName)) {
				found = true;
			}
		}
		if (!found) {
			std::cout << "Cannot find layer: " << layer_names[i] << std::endl;
			return false;
		}
	}
	return true;
}

int VulkanContext::GetPhysicalDeviceByName(std::string name) {
	for (size_t i = 0; i < physical_devices_.size(); i++) {
		if (physicalDeviceProperties_[i].deviceName == name)
			return (int)i;
	}
	return -1;
}

int VulkanContext::GetBestPhysicalDevice() {
	// Rules: Prefer discrete over embedded.
	// Prefer nVidia over Intel.

	int maxScore = -1;
	int best = -1;

	for (size_t i = 0; i < physical_devices_.size(); i++) {
		int score = 0;
		VkPhysicalDeviceProperties props;
		vkGetPhysicalDeviceProperties(physical_devices_[i], &props);
		switch (props.deviceType) {
		case VK_PHYSICAL_DEVICE_TYPE_CPU:
			score += 1;
			break;
		case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
			score += 20;
			break;
		case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
			score += 10;
			break;
		}
		if (props.vendorID == VULKAN_VENDOR_AMD) {
			score += 5;
		} else if (props.vendorID == VULKAN_VENDOR_NVIDIA) {
			score += 5;
		}
		if (score > maxScore) {
			best = (int)i;
			maxScore = score;
		}
	}
	return best;
}

void VulkanContext::ChooseDevice(int physical_device) {
	physical_device_ = physical_device;
	ILOG("Chose physical device %d: %p", physical_device, physical_devices_[physical_device]);

	GetDeviceLayerProperties();
	if (!CheckLayers(device_layer_properties_, device_layer_names_)) {
		WLOG("CheckLayers for device %d failed", physical_device);
	}

	vkGetPhysicalDeviceQueueFamilyProperties(physical_devices_[physical_device_], &queue_count, nullptr);
	assert(queue_count >= 1);

	queue_props.resize(queue_count);
	vkGetPhysicalDeviceQueueFamilyProperties(physical_devices_[physical_device_], &queue_count, queue_props.data());
	assert(queue_count >= 1);

	// Detect preferred formats, in this order.
	static const VkFormat depthStencilFormats[] = {
		VK_FORMAT_D24_UNORM_S8_UINT,
		VK_FORMAT_D32_SFLOAT_S8_UINT,
		VK_FORMAT_D16_UNORM_S8_UINT,
	};
	deviceInfo_.preferredDepthStencilFormat = VK_FORMAT_UNDEFINED;
	for (size_t i = 0; i < ARRAY_SIZE(depthStencilFormats); i++) {
		VkFormatProperties props;
		vkGetPhysicalDeviceFormatProperties(physical_devices_[physical_device_], depthStencilFormats[i], &props);
		if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
			deviceInfo_.preferredDepthStencilFormat = depthStencilFormats[i];
			break;
		}
	}
	if (deviceInfo_.preferredDepthStencilFormat == VK_FORMAT_UNDEFINED) {
		// WTF? This is bad.
		ELOG("Could not find a usable depth stencil format.");
	}

	// This is as good a place as any to do this
	vkGetPhysicalDeviceMemoryProperties(physical_devices_[physical_device_], &memory_properties);

	// Optional features
	vkGetPhysicalDeviceFeatures(physical_devices_[physical_device_], &featuresAvailable_);
	memset(&featuresEnabled_, 0, sizeof(featuresEnabled_));

	// Enable a few safe ones if they are available.
	if (featuresAvailable_.dualSrcBlend) {
		featuresEnabled_.dualSrcBlend = true;
	}
	if (featuresAvailable_.largePoints) {
		featuresEnabled_.largePoints = true;
	}
	if (featuresAvailable_.wideLines) {
		featuresEnabled_.wideLines = true;
	}
	if (featuresAvailable_.geometryShader) {
		featuresEnabled_.geometryShader = true;
	}
	if (featuresAvailable_.logicOp) {
		featuresEnabled_.logicOp = true;
	}
	if (featuresAvailable_.depthClamp) {
		featuresEnabled_.depthClamp = true;
	}
	if (featuresAvailable_.depthBounds) {
		featuresEnabled_.depthBounds = true;
	}
	if (featuresAvailable_.samplerAnisotropy) {
		featuresEnabled_.samplerAnisotropy = true;
	}
	// For easy wireframe mode, someday.
	if (featuresEnabled_.fillModeNonSolid) {
		featuresEnabled_.fillModeNonSolid = true;
	}

	GetDeviceLayerExtensionList(nullptr, device_extension_properties_);

	device_extensions_enabled_.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
}

bool VulkanContext::EnableDeviceExtension(const char *extension) {
	for (auto &iter : device_extension_properties_) {
		if (!strcmp(iter.extensionName, extension)) {
			device_extensions_enabled_.push_back(extension);
			return true;
		}
	}
	return false;
}

VkResult VulkanContext::CreateDevice() {
	if (!init_error_.empty() || physical_device_ < 0) {
		ELOG("Vulkan init failed: %s", init_error_.c_str());
		return VK_ERROR_INITIALIZATION_FAILED;
	}

	VkDeviceQueueCreateInfo queue_info{ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
	float queue_priorities[1] = { 1.0f };
	queue_info.queueCount = 1;
	queue_info.pQueuePriorities = queue_priorities;
	bool found = false;
	for (int i = 0; i < (int)queue_count; i++) {
		if (queue_props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
			queue_info.queueFamilyIndex = i;
			found = true;
			break;
		}
	}
	assert(found);

	deviceExtensionsLookup_.DEDICATED_ALLOCATION = EnableDeviceExtension(VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME);

	VkDeviceCreateInfo device_info{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
	device_info.queueCreateInfoCount = 1;
	device_info.pQueueCreateInfos = &queue_info;
	device_info.enabledLayerCount = (uint32_t)device_layer_names_.size();
	device_info.ppEnabledLayerNames = device_info.enabledLayerCount ? device_layer_names_.data() : nullptr;
	device_info.enabledExtensionCount = (uint32_t)device_extensions_enabled_.size();
	device_info.ppEnabledExtensionNames = device_info.enabledExtensionCount ? device_extensions_enabled_.data() : nullptr;
	device_info.pEnabledFeatures = &featuresEnabled_;
	VkResult res = vkCreateDevice(physical_devices_[physical_device_], &device_info, nullptr, &device_);
	if (res != VK_SUCCESS) {
		init_error_ = "Unable to create Vulkan device";
		ELOG("Unable to create Vulkan device");
	} else {
		VulkanLoadDeviceFunctions(device_);
	}
	ILOG("Device created.\n");
	VulkanSetAvailable(true);
	return res;
}

VkResult VulkanContext::InitDebugMsgCallback(PFN_vkDebugReportCallbackEXT dbgFunc, int bits, void *userdata) {
	VkDebugReportCallbackEXT msg_callback;

	if (!(flags_ & VULKAN_FLAG_VALIDATE)) {
		WLOG("Not registering debug report callback - extension not enabled!");
		return VK_SUCCESS;
	}
	ILOG("Registering debug report callback");

	VkDebugReportCallbackCreateInfoEXT cb = {};
	cb.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CREATE_INFO_EXT;
	cb.pNext = nullptr;
	cb.flags = bits;
	cb.pfnCallback = dbgFunc;
	cb.pUserData = userdata;
	VkResult res = dyn_vkCreateDebugReportCallbackEXT(instance_, &cb, nullptr, &msg_callback);
	switch (res) {
	case VK_SUCCESS:
		msg_callbacks.push_back(msg_callback);
		break;
	case VK_ERROR_OUT_OF_HOST_MEMORY:
		return VK_ERROR_INITIALIZATION_FAILED;
	default:
		return VK_ERROR_INITIALIZATION_FAILED;
	}
	return res;
}

void VulkanContext::DestroyDebugMsgCallback() {
	while (msg_callbacks.size() > 0) {
		dyn_vkDestroyDebugReportCallbackEXT(instance_, msg_callbacks.back(), nullptr);
		msg_callbacks.pop_back();
	}
}

void VulkanContext::InitSurface(WindowSystem winsys, void *data1, void *data2, int width, int height) {
	winsys_ = winsys;
	winsysData1_ = data1;
	winsysData2_ = data2;
	ReinitSurface(width, height);
}

void VulkanContext::ReinitSurface(int width, int height) {
	if (surface_ != VK_NULL_HANDLE) {
		ILOG("Destroying Vulkan surface (%d, %d)", width_, height_);
		vkDestroySurfaceKHR(instance_, surface_, nullptr);
		surface_ = VK_NULL_HANDLE;
	}

	ILOG("Creating Vulkan surface (%d, %d)", width, height);
	switch (winsys_) {
#ifdef _WIN32
	case WINDOWSYSTEM_WIN32:
	{
		HINSTANCE connection = (HINSTANCE)winsysData1_;
		HWND window = (HWND)winsysData2_;

		if (width < 0 || height < 0)
		{
			RECT rc;
			GetClientRect(window, &rc);
			width = rc.right - rc.left;
			height = rc.bottom - rc.top;
		}

		VkWin32SurfaceCreateInfoKHR win32{ VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR };
		win32.flags = 0;
		win32.hwnd = window;
		win32.hinstance = connection;
		VkResult res = vkCreateWin32SurfaceKHR(instance_, &win32, nullptr, &surface_);
		assert(res == VK_SUCCESS);
		break;
	}
#endif
#if defined(__ANDROID__)
	case WINDOWSYSTEM_ANDROID:
	{
		ANativeWindow *wnd = (ANativeWindow *)winsysData1_;
		VkAndroidSurfaceCreateInfoKHR android{ VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR };
		android.flags = 0;
		android.window = wnd;
		VkResult res = vkCreateAndroidSurfaceKHR(instance_, &android, nullptr, &surface_);
		assert(res == VK_SUCCESS);
		break;
	}
#endif
#if defined(VK_USE_PLATFORM_XLIB_KHR)
	case WINDOWSYSTEM_XLIB:
	{
		VkXlibSurfaceCreateInfoKHR xlib = { VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR };
		xlib.flags = 0;
		xlib.dpy = (Display *)winsysData1_;
		xlib.window = (Window)winsysData2_;
		VkResult res = vkCreateXlibSurfaceKHR(instance_, &xlib, nullptr, &surface_);
		assert(res == VK_SUCCESS);
		break;
	}
#endif
#if defined(VK_USE_PLATFORM_XCB_KHR)
	case WINDOWSYSTEM_XCB:
	{
		VkXCBSurfaceCreateInfoKHR xcb = { VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR };
		xcb.flags = 0;
		xcb.connection = (Connection *)winsysData1_;
		xcb.window = (Window)(uintptr_t)winsysData2_;
		VkResult res = vkCreateXcbSurfaceKHR(instance_, &xcb, nullptr, &surface_);
		assert(res == VK_SUCCESS);
		break;
	}
#endif
#if defined(VK_USE_PLATFORM_WAYLAND_KHR)
	case WINDOWSYSTEM_WAYLAND:
	{
		VkWaylandSurfaceCreateInfoKHR wayland = { VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR };
		wayland.flags = 0;
		wayland.display = (wl_display *)winsysData1_;
		wayland.surface = (wl_surface *)winsysData2_;
		VkResult res = vkCreateWaylandSurfaceKHR(instance_, &wayland, nullptr, &surface_);
		assert(res == VK_SUCCESS);
		break;
	}
#endif

	default:
		_assert_msg_(G3D, false, "Vulkan support for chosen window system not implemented");
		break;
	}
	width_ = width;
	height_ = height;
}

bool VulkanContext::InitQueue() {
	// Iterate over each queue to learn whether it supports presenting:
	VkBool32 *supportsPresent = new VkBool32[queue_count];
	for (uint32_t i = 0; i < queue_count; i++) {
		vkGetPhysicalDeviceSurfaceSupportKHR(physical_devices_[physical_device_], i, surface_, &supportsPresent[i]);
	}

	// Search for a graphics queue and a present queue in the array of queue
	// families, try to find one that supports both
	uint32_t graphicsQueueNodeIndex = UINT32_MAX;
	uint32_t presentQueueNodeIndex = UINT32_MAX;
	for (uint32_t i = 0; i < queue_count; i++) {
		if ((queue_props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
			if (graphicsQueueNodeIndex == UINT32_MAX) {
				graphicsQueueNodeIndex = i;
			}

			if (supportsPresent[i] == VK_TRUE) {
				graphicsQueueNodeIndex = i;
				presentQueueNodeIndex = i;
				break;
			}
		}
	}
	if (presentQueueNodeIndex == UINT32_MAX) {
		// If didn't find a queue that supports both graphics and present, then
		// find a separate present queue.
		for (uint32_t i = 0; i < queue_count; ++i) {
			if (supportsPresent[i] == VK_TRUE) {
				presentQueueNodeIndex = i;
				break;
			}
		}
	}
	delete[] supportsPresent;

	// Generate error if could not find both a graphics and a present queue
	if (graphicsQueueNodeIndex == UINT32_MAX || presentQueueNodeIndex == UINT32_MAX) {
		ELOG("Could not find a graphics and a present queue");
		return false;
	}

	graphics_queue_family_index_ = graphicsQueueNodeIndex;

	// Get the list of VkFormats that are supported:
	uint32_t formatCount = 0;
	VkResult res = vkGetPhysicalDeviceSurfaceFormatsKHR(physical_devices_[physical_device_], surface_, &formatCount, nullptr);
	_assert_msg_(G3D, res == VK_SUCCESS, "Failed to get formats for device %p: %d surface: %p", physical_devices_[physical_device_], (int)res, surface_);
	if (res != VK_SUCCESS) {
		return false;
	}

	std::vector<VkSurfaceFormatKHR> surfFormats(formatCount);
	res = vkGetPhysicalDeviceSurfaceFormatsKHR(physical_devices_[physical_device_], surface_, &formatCount, surfFormats.data());
	assert(res == VK_SUCCESS);
	if (res != VK_SUCCESS) {
		return false;
	}
	// If the format list includes just one entry of VK_FORMAT_UNDEFINED,
	// the surface has no preferred format.  Otherwise, at least one
	// supported format will be returned.
	if (formatCount == 0 || (formatCount == 1 && surfFormats[0].format == VK_FORMAT_UNDEFINED)) {
		ILOG("swapchain_format: Falling back to B8G8R8A8_UNORM");
		swapchainFormat_ = VK_FORMAT_B8G8R8A8_UNORM;
	} else {
		swapchainFormat_ = VK_FORMAT_UNDEFINED;
		for (uint32_t i = 0; i < formatCount; ++i) {
			if (surfFormats[i].colorSpace != VK_COLORSPACE_SRGB_NONLINEAR_KHR) {
				continue;
			}

			if (surfFormats[i].format == VK_FORMAT_B8G8R8A8_UNORM || surfFormats[i].format == VK_FORMAT_R8G8B8A8_UNORM) {
				swapchainFormat_ = surfFormats[i].format;
				break;
			}
		}
		if (swapchainFormat_ == VK_FORMAT_UNDEFINED) {
			// Okay, take the first one then.
			swapchainFormat_ = surfFormats[0].format;
		}
		ILOG("swapchain_format: %d (/%d)", swapchainFormat_, formatCount);
	}

	vkGetDeviceQueue(device_, graphics_queue_family_index_, 0, &gfx_queue_);
	ILOG("gfx_queue_: %p", gfx_queue_);
	return true;
}

bool VulkanContext::InitSwapchain() {
	VkResult res = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_devices_[physical_device_], surface_, &surfCapabilities_);
	assert(res == VK_SUCCESS);
	uint32_t presentModeCount;
	res = vkGetPhysicalDeviceSurfacePresentModesKHR(physical_devices_[physical_device_], surface_, &presentModeCount, nullptr);
	assert(res == VK_SUCCESS);
	VkPresentModeKHR *presentModes = new VkPresentModeKHR[presentModeCount];
	assert(presentModes);
	res = vkGetPhysicalDeviceSurfacePresentModesKHR(physical_devices_[physical_device_], surface_, &presentModeCount, presentModes);
	assert(res == VK_SUCCESS);

	VkExtent2D swapChainExtent;
	// width and height are either both -1, or both not -1.
	if (surfCapabilities_.currentExtent.width == (uint32_t)-1) {
		// If the surface size is undefined, the size is set to
		// the size of the images requested.
		ILOG("initSwapchain: %dx%d", width_, height_);
		swapChainExtent.width = width_;
		swapChainExtent.height = height_;
	} else {
		// If the surface size is defined, the swap chain size must match
		swapChainExtent = surfCapabilities_.currentExtent;
	}

	// TODO: Find a better way to specify the prioritized present mode while being able
	// to fall back in a sensible way.
	VkPresentModeKHR swapchainPresentMode = VK_PRESENT_MODE_MAX_ENUM_KHR;
	for (size_t i = 0; i < presentModeCount; i++) {
		ILOG("Supported present mode: %d (%s)", presentModes[i], PresentModeString(presentModes[i]));
	}
	for (size_t i = 0; i < presentModeCount; i++) {
		if (swapchainPresentMode == VK_PRESENT_MODE_MAX_ENUM_KHR) {
			// Default to the first present mode from the list.
			swapchainPresentMode = presentModes[i];
		}
		if ((flags_ & VULKAN_FLAG_PRESENT_MAILBOX) && presentModes[i] == VK_PRESENT_MODE_MAILBOX_KHR) {
			swapchainPresentMode = VK_PRESENT_MODE_MAILBOX_KHR;
			break;
		}
		if ((flags_ & VULKAN_FLAG_PRESENT_FIFO_RELAXED) && presentModes[i] == VK_PRESENT_MODE_FIFO_RELAXED_KHR) {
			swapchainPresentMode = VK_PRESENT_MODE_FIFO_RELAXED_KHR;
			break;
		}
		if ((flags_ & VULKAN_FLAG_PRESENT_IMMEDIATE) && presentModes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR) {
			swapchainPresentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
			break;
		}
	}
#ifdef __ANDROID__
	// HACK
	swapchainPresentMode = VK_PRESENT_MODE_FIFO_KHR;
#endif
	ILOG("Chosen present mode: %d (%s)", swapchainPresentMode, PresentModeString(swapchainPresentMode));
	delete[] presentModes;
	// Determine the number of VkImage's to use in the swap chain (we desire to
	// own only 1 image at a time, besides the images being displayed and
	// queued for display):
	uint32_t desiredNumberOfSwapChainImages = surfCapabilities_.minImageCount + 1;
	ILOG("numSwapChainImages: %d", desiredNumberOfSwapChainImages);
	if ((surfCapabilities_.maxImageCount > 0) &&
		(desiredNumberOfSwapChainImages > surfCapabilities_.maxImageCount))
	{
		// Application must settle for fewer images than desired:
		desiredNumberOfSwapChainImages = surfCapabilities_.maxImageCount;
	}

	VkSurfaceTransformFlagBitsKHR preTransform;
	if (surfCapabilities_.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) {
		preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
	} else {
		preTransform = surfCapabilities_.currentTransform;
	}

	VkSwapchainCreateInfoKHR swap_chain_info = { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
	swap_chain_info.surface = surface_;
	swap_chain_info.minImageCount = desiredNumberOfSwapChainImages;
	swap_chain_info.imageFormat = swapchainFormat_;
	swap_chain_info.imageColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR;
	swap_chain_info.imageExtent.width = swapChainExtent.width;
	swap_chain_info.imageExtent.height = swapChainExtent.height;
	swap_chain_info.preTransform = preTransform;
	swap_chain_info.imageArrayLayers = 1;
	swap_chain_info.presentMode = swapchainPresentMode;
	swap_chain_info.oldSwapchain = VK_NULL_HANDLE;
	swap_chain_info.clipped = true;
	swap_chain_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	if (surfCapabilities_.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT)
		swap_chain_info.imageUsage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;

#ifndef ANDROID
	// We don't support screenshots on Android
	// Add more usage flags if they're supported.
	if (surfCapabilities_.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
		swap_chain_info.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
#endif

	swap_chain_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	swap_chain_info.queueFamilyIndexCount = 0;
	swap_chain_info.pQueueFamilyIndices = NULL;
	// OPAQUE is not supported everywhere.
	if (surfCapabilities_.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) {
		swap_chain_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	} else {
		// This should be supported anywhere, and is the only thing supported on the SHIELD TV, for example.
		swap_chain_info.compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
	}

	res = vkCreateSwapchainKHR(device_, &swap_chain_info, NULL, &swapchain_);
	if (res != VK_SUCCESS) {
		ELOG("vkCreateSwapchainKHR failed!");
		return false;
	}

	return true;
}

VkFence VulkanContext::CreateFence(bool presignalled) {
	VkFence fence;
	VkFenceCreateInfo fenceInfo{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
	fenceInfo.flags = presignalled ? VK_FENCE_CREATE_SIGNALED_BIT : 0;
	vkCreateFence(device_, &fenceInfo, NULL, &fence);
	return fence;
}

void VulkanContext::PerformPendingDeletes() {
	for (int i = 0; i < ARRAY_SIZE(frame_); i++) {
		frame_[i].deleteList.PerformDeletes(device_);
	}
	Delete().PerformDeletes(device_);
}

void VulkanContext::DestroyDevice() {
	ILOG("VulkanContext::DestroyDevice (performing deletes)");
	PerformPendingDeletes();

	vkDestroyDevice(device_, nullptr);
	device_ = nullptr;
}

bool VulkanContext::CreateShaderModule(const std::vector<uint32_t> &spirv, VkShaderModule *shaderModule) {
	VkShaderModuleCreateInfo sm{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
	sm.pCode = spirv.data();
	sm.codeSize = spirv.size() * sizeof(uint32_t);
	sm.flags = 0;
	VkResult result = vkCreateShaderModule(device_, &sm, nullptr, shaderModule);
	if (result != VK_SUCCESS) {
		return false;
	} else {
		return true;
	}
}

void TransitionImageLayout2(VkCommandBuffer cmd, VkImage image, int baseMip, int numMipLevels, VkImageAspectFlags aspectMask,
	VkImageLayout oldImageLayout, VkImageLayout newImageLayout,
	VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask,
	VkAccessFlags srcAccessMask, VkAccessFlags dstAccessMask) {
#ifdef VULKAN_USE_GENERAL_LAYOUT_FOR_COLOR
	if (aspectMask == VK_IMAGE_ASPECT_COLOR_BIT) {
		// Hack to disable transaction elimination on ARM Mali.
		if (oldImageLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL || oldImageLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
			oldImageLayout = VK_IMAGE_LAYOUT_GENERAL;
		if (newImageLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL || newImageLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
			newImageLayout = VK_IMAGE_LAYOUT_GENERAL;
	}
#endif
#ifdef VULKAN_USE_GENERAL_LAYOUT_FOR_DEPTH_STENCIL
	if (aspectMask != VK_IMAGE_ASPECT_COLOR_BIT) {
		// Hack to disable transaction elimination on ARM Mali.
		if (oldImageLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL || oldImageLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
			oldImageLayout = VK_IMAGE_LAYOUT_GENERAL;
		if (newImageLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL || newImageLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
			newImageLayout = VK_IMAGE_LAYOUT_GENERAL;
	}
#endif
	VkImageMemoryBarrier image_memory_barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
	image_memory_barrier.srcAccessMask = srcAccessMask;
	image_memory_barrier.dstAccessMask = dstAccessMask;
	image_memory_barrier.oldLayout = oldImageLayout;
	image_memory_barrier.newLayout = newImageLayout;
	image_memory_barrier.image = image;
	image_memory_barrier.subresourceRange.aspectMask = aspectMask;
	image_memory_barrier.subresourceRange.baseMipLevel = baseMip;
	image_memory_barrier.subresourceRange.levelCount = numMipLevels;
	image_memory_barrier.subresourceRange.layerCount = 1;  // We never use more than one layer, and old Mali drivers have problems with VK_REMAINING_ARRAY_LAYERS/VK_REMAINING_MIP_LEVELS.
	image_memory_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	image_memory_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	vkCmdPipelineBarrier(cmd, srcStageMask, dstStageMask, 0, 0, nullptr, 0, nullptr, 1, &image_memory_barrier);
}

EShLanguage FindLanguage(const VkShaderStageFlagBits shader_type) {
	switch (shader_type) {
	case VK_SHADER_STAGE_VERTEX_BIT:
		return EShLangVertex;

	case VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT:
		return EShLangTessControl;

	case VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT:
		return EShLangTessEvaluation;

	case VK_SHADER_STAGE_GEOMETRY_BIT:
		return EShLangGeometry;

	case VK_SHADER_STAGE_FRAGMENT_BIT:
		return EShLangFragment;

	case VK_SHADER_STAGE_COMPUTE_BIT:
		return EShLangCompute;

	default:
		return EShLangVertex;
	}
}

// Compile a given string containing GLSL into SPV for use by VK
// Return value of false means an error was encountered.
bool GLSLtoSPV(const VkShaderStageFlagBits shader_type,
	const char *pshader,
	std::vector<unsigned int> &spirv, std::string *errorMessage) {

	glslang::TProgram program;
	const char *shaderStrings[1];
	TBuiltInResource Resources;
	init_resources(Resources);

	// Enable SPIR-V and Vulkan rules when parsing GLSL
	EShMessages messages = (EShMessages)(EShMsgSpvRules | EShMsgVulkanRules);

	EShLanguage stage = FindLanguage(shader_type);
	glslang::TShader shader(stage);

	shaderStrings[0] = pshader;
	shader.setStrings(shaderStrings, 1);

	if (!shader.parse(&Resources, 100, false, messages)) {
		puts(shader.getInfoLog());
		puts(shader.getInfoDebugLog());
		if (errorMessage) {
			*errorMessage = shader.getInfoLog();
			(*errorMessage) += shader.getInfoDebugLog();
		}
		return false; // something didn't work
	}

	// Note that program does not take ownership of &shader, so this is fine.
	program.addShader(&shader);

	if (!program.link(messages)) {
		puts(shader.getInfoLog());
		puts(shader.getInfoDebugLog());
		if (errorMessage) {
			*errorMessage = shader.getInfoLog();
			(*errorMessage) += shader.getInfoDebugLog();
		}
		return false;
	}

	// Can't fail, parsing worked, "linking" worked.
	glslang::GlslangToSpv(*program.getIntermediate(stage), spirv);
	return true;
}

void init_glslang() {
	glslang::InitializeProcess();
}

void finalize_glslang() {
	glslang::FinalizeProcess();
}

const char *VulkanResultToString(VkResult res) {
	switch (res) {
	case VK_NOT_READY: return "VK_NOT_READY";
	case VK_TIMEOUT: return "VK_TIMEOUT";
	case VK_EVENT_SET: return "VK_EVENT_SET";
	case VK_EVENT_RESET: return "VK_EVENT_RESET";
	case VK_INCOMPLETE: return "VK_INCOMPLETE";
	case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
	case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
	case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
	case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
	case VK_ERROR_MEMORY_MAP_FAILED: return "VK_ERROR_MEMORY_MAP_FAILED";
	case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
	case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
	case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
	case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
	case VK_ERROR_TOO_MANY_OBJECTS: return "VK_ERROR_TOO_MANY_OBJECTS";
	case VK_ERROR_FORMAT_NOT_SUPPORTED: return "VK_ERROR_FORMAT_NOT_SUPPORTED";
	case VK_ERROR_SURFACE_LOST_KHR: return "VK_ERROR_SURFACE_LOST_KHR";
	case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
	case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
	case VK_ERROR_INCOMPATIBLE_DISPLAY_KHR: return "VK_ERROR_INCOMPATIBLE_DISPLAY_KHR";
	case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR: return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
	case VK_ERROR_OUT_OF_POOL_MEMORY_KHR: return "VK_ERROR_OUT_OF_POOL_MEMORY_KHR";
	case VK_ERROR_INVALID_EXTERNAL_HANDLE_KHR: return "VK_ERROR_INVALID_EXTERNAL_HANDLE_KHR";

	default:
		return "VK_ERROR_...(unknown)";
	}
}

void VulkanDeleteList::Take(VulkanDeleteList &del) {
	assert(cmdPools_.empty());
	assert(descPools_.empty());
	assert(modules_.empty());
	assert(buffers_.empty());
	assert(bufferViews_.empty());
	assert(images_.empty());
	assert(imageViews_.empty());
	assert(deviceMemory_.empty());
	assert(samplers_.empty());
	assert(pipelines_.empty());
	assert(pipelineCaches_.empty());
	assert(renderPasses_.empty());
	assert(framebuffers_.empty());
	assert(pipelineLayouts_.empty());
	assert(descSetLayouts_.empty());
	assert(callbacks_.empty());
	cmdPools_ = std::move(del.cmdPools_);
	descPools_ = std::move(del.descPools_);
	modules_ = std::move(del.modules_);
	buffers_ = std::move(del.buffers_);
	bufferViews_ = std::move(del.bufferViews_);
	images_ = std::move(del.images_);
	imageViews_ = std::move(del.imageViews_);
	deviceMemory_ = std::move(del.deviceMemory_);
	samplers_ = std::move(del.samplers_);
	pipelines_ = std::move(del.pipelines_);
	pipelineCaches_ = std::move(del.pipelineCaches_);
	renderPasses_ = std::move(del.renderPasses_);
	framebuffers_ = std::move(del.framebuffers_);
	pipelineLayouts_ = std::move(del.pipelineLayouts_);
	descSetLayouts_ = std::move(del.descSetLayouts_);
	callbacks_ = std::move(del.callbacks_);
	del.cmdPools_.clear();
	del.descPools_.clear();
	del.modules_.clear();
	del.buffers_.clear();
	del.images_.clear();
	del.imageViews_.clear();
	del.deviceMemory_.clear();
	del.samplers_.clear();
	del.pipelines_.clear();
	del.pipelineCaches_.clear();
	del.renderPasses_.clear();
	del.framebuffers_.clear();
	del.pipelineLayouts_.clear();
	del.descSetLayouts_.clear();
	del.callbacks_.clear();
}

void VulkanDeleteList::PerformDeletes(VkDevice device) {
	for (auto &callback : callbacks_) {
		callback.func(callback.userdata);
	}
	callbacks_.clear();
	for (auto &cmdPool : cmdPools_) {
		vkDestroyCommandPool(device, cmdPool, nullptr);
	}
	cmdPools_.clear();
	for (auto &descPool : descPools_) {
		vkDestroyDescriptorPool(device, descPool, nullptr);
	}
	descPools_.clear();
	for (auto &module : modules_) {
		vkDestroyShaderModule(device, module, nullptr);
	}
	modules_.clear();
	for (auto &buf : buffers_) {
		vkDestroyBuffer(device, buf, nullptr);
	}
	buffers_.clear();
	for (auto &bufView : bufferViews_) {
		vkDestroyBufferView(device, bufView, nullptr);
	}
	bufferViews_.clear();
	for (auto &image : images_) {
		vkDestroyImage(device, image, nullptr);
	}
	images_.clear();
	for (auto &imageView : imageViews_) {
		vkDestroyImageView(device, imageView, nullptr);
	}
	imageViews_.clear();
	for (auto &mem : deviceMemory_) {
		vkFreeMemory(device, mem, nullptr);
	}
	deviceMemory_.clear();
	for (auto &sampler : samplers_) {
		vkDestroySampler(device, sampler, nullptr);
	}
	samplers_.clear();
	for (auto &pipeline : pipelines_) {
		vkDestroyPipeline(device, pipeline, nullptr);
	}
	pipelines_.clear();
	for (auto &pcache : pipelineCaches_) {
		vkDestroyPipelineCache(device, pcache, nullptr);
	}
	pipelineCaches_.clear();
	for (auto &renderPass : renderPasses_) {
		vkDestroyRenderPass(device, renderPass, nullptr);
	}
	renderPasses_.clear();
	for (auto &framebuffer : framebuffers_) {
		vkDestroyFramebuffer(device, framebuffer, nullptr);
	}
	framebuffers_.clear();
	for (auto &pipeLayout : pipelineLayouts_) {
		vkDestroyPipelineLayout(device, pipeLayout, nullptr);
	}
	pipelineLayouts_.clear();
	for (auto &descSetLayout : descSetLayouts_) {
		vkDestroyDescriptorSetLayout(device, descSetLayout, nullptr);
	}
	descSetLayouts_.clear();
}
