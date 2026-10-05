// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jonathan Ferguson
//
// Vulkan ray tracing backend. The scene lives on the GPU as two acceleration
// structures, one for the map and one rebuilt every frame for what moves; a
// compute shader traces a ray per pixel through them (ray queries) into a
// picture that the last pass puts on screen under the overlay.
//
// So far it shows what the eye sees, simply shaded. The lighting, the
// denoiser and the rest of what the CPU backend does are still to come.

#include "../include/pt.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define VK_USE_PLATFORM_WIN32_KHR
#include <windows.h>
#include <vulkan/vulkan.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

const uint32_t blit_vert_spv[] =
#include "blit.vert.inc"
;
const uint32_t blit_frag_spv[] =
#include "blit.frag.inc"
;
const uint32_t trace_comp_spv[] =
#include "trace.comp.inc"
;

const uint32_t VENDOR_NVIDIA = 0x10DE;

const char *const kDeviceExtensions[] = {
	VK_KHR_SWAPCHAIN_EXTENSION_NAME,
	VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
	VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
	VK_KHR_RAY_QUERY_EXTENSION_NAME,
};
const uint32_t kNumDeviceExtensions = sizeof(kDeviceExtensions) / sizeof(kDeviceExtensions[0]);

struct Push
{
	float	view[4];
	float	screen[2];
	float	has_view;		// there is a traced picture to show in the view
};

// what the tracing shader is told about the view; see trace.comp
struct TracePush
{
	float	origin[4];
	float	forward[4];
	float	right[4];
	float	up[4];
	int32_t	sky[8];
};

// one triangle and one material as the shaders read them (std430)
struct GpuTri
{
	float		uv[6];
	uint32_t	material;
	uint32_t	pad;
};

struct GpuMaterial
{
	int32_t		texture;
	uint32_t	flags;
	float		alpha;
	float		roughness;
	float		emission[4];
};

const uint32_t kMaxTextures = 4096;

struct Buffer
{
	VkBuffer		buffer = VK_NULL_HANDLE;
	VkDeviceMemory	memory = VK_NULL_HANDLE;
	VkDeviceSize	size = 0;
	void			*ptr = nullptr;			// where it is mapped, if it is
	VkDeviceAddress	address = 0;
};

struct Texture
{
	VkImage			image = VK_NULL_HANDLE;
	VkDeviceMemory	memory = VK_NULL_HANDLE;
	VkImageView		view = VK_NULL_HANDLE;
	int				width = 0, height = 0;
};

struct Accel
{
	VkAccelerationStructureKHR	as = VK_NULL_HANDLE;
	Buffer						storage;
	Buffer						scratch;
	VkDeviceAddress				scratch_address = 0;	// aligned as the build wants
	VkDeviceAddress				address = 0;
};

// the triangles of the map or of a frame, as the GPU holds them
struct Geometry
{
	Buffer		corners;		// 9 floats a triangle
	Buffer		tris;			// GpuTri
	Buffer		materials;		// GpuMaterial
	Accel		blas;
	uint32_t	num_tris = 0;
	uint32_t	room_tris = 0, room_materials = 0;
};

struct RtxBackend
{
	pt_backend_t	base{};
	pt_log_fn		log = nullptr;
	HWND			hwnd = nullptr;
	int				width = 0, height = 0;

	VkInstance			instance = VK_NULL_HANDLE;
	VkSurfaceKHR		surface = VK_NULL_HANDLE;
	VkPhysicalDevice	gpu = VK_NULL_HANDLE;
	VkDevice			device = VK_NULL_HANDLE;
	uint32_t			queue_family = 0;
	VkQueue				queue = VK_NULL_HANDLE;

	VkCommandPool		cmdpool = VK_NULL_HANDLE;
	VkCommandBuffer		cmd = VK_NULL_HANDLE;
	VkCommandBuffer		cmd_once = VK_NULL_HANDLE;	// for work done at once, outside a frame
	VkFence				fence = VK_NULL_HANDLE;
	VkSemaphore			sem_acquire = VK_NULL_HANDLE;

	VkSwapchainKHR		swapchain = VK_NULL_HANDLE;
	VkSurfaceFormatKHR	surface_format{};
	VkExtent2D			extent{};
	std::vector<VkImage>		swap_images;
	std::vector<VkImageView>	swap_views;
	// signalled when an image has been drawn; one each, because presenting
	// may hold on to it until that image comes round again
	std::vector<VkSemaphore>	swap_drawn;

	VkImage				ov_image = VK_NULL_HANDLE;
	VkDeviceMemory		ov_memory = VK_NULL_HANDLE;
	VkImageView			ov_view = VK_NULL_HANDLE;
	VkSampler			ov_sampler = VK_NULL_HANDLE;
	VkBuffer			staging = VK_NULL_HANDLE;
	VkDeviceMemory		staging_memory = VK_NULL_HANDLE;
	void				*staging_ptr = nullptr;
	bool				ov_initialized = false;

	VkDescriptorSetLayout	dsl = VK_NULL_HANDLE;
	VkDescriptorPool		dpool = VK_NULL_HANDLE;
	VkDescriptorSet			dset = VK_NULL_HANDLE;
	VkPipelineLayout		playout = VK_NULL_HANDLE;
	VkPipeline				pipeline = VK_NULL_HANDLE;

	bool		has_view = false;
	pt_view_t	view{};

	// extension entry points, which the loader does not export
	PFN_vkCreateAccelerationStructureKHR			CreateAccelerationStructure = nullptr;
	PFN_vkDestroyAccelerationStructureKHR			DestroyAccelerationStructure = nullptr;
	PFN_vkGetAccelerationStructureBuildSizesKHR		GetAccelerationStructureBuildSizes = nullptr;
	PFN_vkCmdBuildAccelerationStructuresKHR			CmdBuildAccelerationStructures = nullptr;
	PFN_vkGetAccelerationStructureDeviceAddressKHR	GetAccelerationStructureDeviceAddress = nullptr;
	VkDebugUtilsMessengerEXT	messenger = VK_NULL_HANDLE;
	uint32_t					scratch_align = 1;

	// the scene
	std::vector<Texture>	textures;			// kMaxTextures slots; the shaders see them all
	std::vector<int>		world_textures;		// slots the map's textures were given
	VkSampler				tex_sampler = VK_NULL_HANDLE;
	Texture					blank;				// stands in every slot that holds nothing
	Geometry				world, frame;
	bool					world_loaded = false;
	int32_t					sky[6] = {-1, -1, -1, -1, -1, -1};
	Accel					tlas;
	Buffer					instances;

	// the traced picture
	VkImage					trace_image = VK_NULL_HANDLE;
	VkDeviceMemory			trace_memory = VK_NULL_HANDLE;
	VkImageView				trace_view = VK_NULL_HANDLE;
	VkSampler				trace_sampler = VK_NULL_HANDLE;
	int						trace_width = 0, trace_height = 0;
	VkDescriptorSetLayout	trace_dsl = VK_NULL_HANDLE;
	VkDescriptorPool		trace_dpool = VK_NULL_HANDLE;
	VkDescriptorSet			trace_dset = VK_NULL_HANDLE;
	VkPipelineLayout		trace_playout = VK_NULL_HANDLE;
	VkPipeline				trace_pipeline = VK_NULL_HANDLE;
	bool					trace_ready = false;	// this frame's view can be traced

	char		stats[160] = "";
};

RtxBackend *Self(pt_backend_t *b) { return reinterpret_cast<RtxBackend *>(b); }

void Logf(const RtxBackend *s, const char *fmt, ...)
{
	if (!s->log)
		return;
	char buf[1024];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	s->log(buf);
}

struct Fail
{
	std::string	msg;
};

[[noreturn]] void Throw(const char *fmt, ...)
{
	char buf[1024];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	throw Fail{buf};
}

void Check(VkResult r, const char *what)
{
	if (r != VK_SUCCESS)
		Throw("%s failed (VkResult %d)", what, (int)r);
}

// ---------------------------------------------------------------- device

bool HasExtensions(VkPhysicalDevice gpu, std::string &missing)
{
	uint32_t n = 0;
	vkEnumerateDeviceExtensionProperties(gpu, nullptr, &n, nullptr);
	std::vector<VkExtensionProperties> exts(n);
	vkEnumerateDeviceExtensionProperties(gpu, nullptr, &n, exts.data());

	for (uint32_t i = 0; i < kNumDeviceExtensions; i++)
	{
		bool found = false;
		for (const VkExtensionProperties &e : exts)
			if (!strcmp(e.extensionName, kDeviceExtensions[i]))
				found = true;
		if (!found)
		{
			missing = kDeviceExtensions[i];
			return false;
		}
	}
	return true;
}

bool FindQueueFamily(const RtxBackend *s, VkPhysicalDevice gpu, uint32_t &family)
{
	uint32_t n = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(gpu, &n, nullptr);
	std::vector<VkQueueFamilyProperties> props(n);
	vkGetPhysicalDeviceQueueFamilyProperties(gpu, &n, props.data());

	for (uint32_t i = 0; i < n; i++)
	{
		VkBool32 present = VK_FALSE;
		vkGetPhysicalDeviceSurfaceSupportKHR(gpu, i, s->surface, &present);
		if (present && (props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT))
		{
			family = i;
			return true;
		}
	}
	return false;
}

struct RtFeatures
{
	VkPhysicalDeviceFeatures2							f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
	VkPhysicalDeviceVulkan12Features					v12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
	VkPhysicalDeviceVulkan13Features					v13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
	VkPhysicalDeviceAccelerationStructureFeaturesKHR	as{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
	VkPhysicalDeviceRayQueryFeaturesKHR					rq{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};

	RtFeatures()
	{
		f2.pNext = &v12;
		v12.pNext = &v13;
		v13.pNext = &as;
		as.pNext = &rq;
	}
	RtFeatures(const RtFeatures &) = delete;
	RtFeatures &operator=(const RtFeatures &) = delete;
};

void PickDevice(RtxBackend *s)
{
	uint32_t n = 0;
	vkEnumeratePhysicalDevices(s->instance, &n, nullptr);
	std::vector<VkPhysicalDevice> gpus(n);
	vkEnumeratePhysicalDevices(s->instance, &n, gpus.data());
	if (!n)
		Throw("no Vulkan devices found");

	std::string reasons;
	for (VkPhysicalDevice gpu : gpus)
	{
		VkPhysicalDeviceProperties props;
		vkGetPhysicalDeviceProperties(gpu, &props);

		std::string why;
		std::string missing;
		RtFeatures feat;
		uint32_t family = 0;

		if (props.vendorID != VENDOR_NVIDIA)
			why = "not an Nvidia GPU";
		else if (props.apiVersion < VK_API_VERSION_1_3)
			why = "driver does not support Vulkan 1.3";
		else if (!HasExtensions(gpu, missing))
			why = "missing " + missing;
		else if (!FindQueueFamily(s, gpu, family))
			why = "cannot present to this window";
		else
		{
			vkGetPhysicalDeviceFeatures2(gpu, &feat.f2);
			if (!feat.as.accelerationStructure || !feat.rq.rayQuery ||
				!feat.v12.bufferDeviceAddress || !feat.v12.runtimeDescriptorArray ||
				!feat.v12.shaderSampledImageArrayNonUniformIndexing ||
				!feat.v13.dynamicRendering || !feat.v13.synchronization2)
				why = "ray tracing features not available";
		}

		if (why.empty())
		{
			s->gpu = gpu;
			s->queue_family = family;
			Logf(s, "RTX path tracer: using %s\n", props.deviceName);
			return;
		}
		if (!reasons.empty())
			reasons += "; ";
		reasons += std::string(props.deviceName) + ": " + why;
	}
	Throw("no usable ray tracing GPU (%s)", reasons.c_str());
}

void CreateDevice(RtxBackend *s)
{
	const float prio = 1.0f;
	VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
	qci.queueFamilyIndex = s->queue_family;
	qci.queueCount = 1;
	qci.pQueuePriorities = &prio;

	// only what the tracer will need; everything here was verified in PickDevice
	RtFeatures feat;
	feat.v12.bufferDeviceAddress = VK_TRUE;
	feat.v13.dynamicRendering = VK_TRUE;
	feat.v13.synchronization2 = VK_TRUE;
	feat.as.accelerationStructure = VK_TRUE;
	feat.rq.rayQuery = VK_TRUE;
	feat.v12.runtimeDescriptorArray = VK_TRUE;
	feat.v12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;

	VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
	dci.pNext = &feat.f2;
	dci.queueCreateInfoCount = 1;
	dci.pQueueCreateInfos = &qci;
	dci.enabledExtensionCount = kNumDeviceExtensions;
	dci.ppEnabledExtensionNames = kDeviceExtensions;
	Check(vkCreateDevice(s->gpu, &dci, nullptr, &s->device), "vkCreateDevice");
	vkGetDeviceQueue(s->device, s->queue_family, 0, &s->queue);

	VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
	pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	pci.queueFamilyIndex = s->queue_family;
	Check(vkCreateCommandPool(s->device, &pci, nullptr, &s->cmdpool), "vkCreateCommandPool");

	VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
	cai.commandPool = s->cmdpool;
	cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	cai.commandBufferCount = 1;
	Check(vkAllocateCommandBuffers(s->device, &cai, &s->cmd), "vkAllocateCommandBuffers");
	Check(vkAllocateCommandBuffers(s->device, &cai, &s->cmd_once), "vkAllocateCommandBuffers");

#define LOAD(name) \
	s->name = (PFN_vk##name##KHR)vkGetDeviceProcAddr(s->device, "vk" #name "KHR"); \
	if (!s->name) Throw("the driver has no vk" #name "KHR")
	LOAD(CreateAccelerationStructure);
	LOAD(DestroyAccelerationStructure);
	LOAD(GetAccelerationStructureBuildSizes);
	LOAD(CmdBuildAccelerationStructures);
	LOAD(GetAccelerationStructureDeviceAddress);
#undef LOAD

	VkPhysicalDeviceAccelerationStructurePropertiesKHR asp{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR};
	VkPhysicalDeviceProperties2 p2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
	p2.pNext = &asp;
	vkGetPhysicalDeviceProperties2(s->gpu, &p2);
	s->scratch_align = std::max(1u, asp.minAccelerationStructureScratchOffsetAlignment);

	VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
	fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
	Check(vkCreateFence(s->device, &fci, nullptr, &s->fence), "vkCreateFence");

	VkSemaphoreCreateInfo sci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
	Check(vkCreateSemaphore(s->device, &sci, nullptr, &s->sem_acquire), "vkCreateSemaphore");
}

// ------------------------------------------------------------- swapchain

void DestroySwapchain(RtxBackend *s)
{
	for (VkImageView v : s->swap_views)
		vkDestroyImageView(s->device, v, nullptr);
	s->swap_views.clear();
	for (VkSemaphore sem : s->swap_drawn)
		vkDestroySemaphore(s->device, sem, nullptr);
	s->swap_drawn.clear();
	s->swap_images.clear();
	if (s->swapchain)
	{
		vkDestroySwapchainKHR(s->device, s->swapchain, nullptr);
		s->swapchain = VK_NULL_HANDLE;
	}
}

// leaves swapchain null while the window has no area (minimized)
void CreateSwapchain(RtxBackend *s)
{
	VkSurfaceCapabilitiesKHR caps;
	Check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(s->gpu, s->surface, &caps), "surface capabilities");

	VkExtent2D extent = caps.currentExtent;
	if (extent.width == 0xFFFFFFFF)
	{
		extent.width = (uint32_t)s->width;
		extent.height = (uint32_t)s->height;
	}
	if (!extent.width || !extent.height)
		return;

	uint32_t n = 0;
	vkGetPhysicalDeviceSurfaceFormatsKHR(s->gpu, s->surface, &n, nullptr);
	std::vector<VkSurfaceFormatKHR> formats(n);
	vkGetPhysicalDeviceSurfaceFormatsKHR(s->gpu, s->surface, &n, formats.data());
	if (!n)
		Throw("surface has no formats");

	// the engine's colours are already display referred, so no sRGB encode
	s->surface_format = formats[0];
	for (const VkSurfaceFormatKHR &f : formats)
		if (f.format == VK_FORMAT_B8G8R8A8_UNORM && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
			s->surface_format = f;

	vkGetPhysicalDeviceSurfacePresentModesKHR(s->gpu, s->surface, &n, nullptr);
	std::vector<VkPresentModeKHR> modes(n);
	vkGetPhysicalDeviceSurfacePresentModesKHR(s->gpu, s->surface, &n, modes.data());

	// the engine paces its own frames, so prefer not to block on vsync
	VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;
	for (VkPresentModeKHR m : modes)
		if (m == VK_PRESENT_MODE_IMMEDIATE_KHR)
			mode = m;
	for (VkPresentModeKHR m : modes)
		if (m == VK_PRESENT_MODE_MAILBOX_KHR)
			mode = m;

	uint32_t count = caps.minImageCount + 1;
	if (caps.maxImageCount && count > caps.maxImageCount)
		count = caps.maxImageCount;

	VkSwapchainCreateInfoKHR sci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
	sci.surface = s->surface;
	sci.minImageCount = count;
	sci.imageFormat = s->surface_format.format;
	sci.imageColorSpace = s->surface_format.colorSpace;
	sci.imageExtent = extent;
	sci.imageArrayLayers = 1;
	sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	sci.preTransform = caps.currentTransform;
	sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	sci.presentMode = mode;
	sci.clipped = VK_TRUE;
	Check(vkCreateSwapchainKHR(s->device, &sci, nullptr, &s->swapchain), "vkCreateSwapchainKHR");
	s->extent = extent;

	vkGetSwapchainImagesKHR(s->device, s->swapchain, &n, nullptr);
	s->swap_images.resize(n);
	vkGetSwapchainImagesKHR(s->device, s->swapchain, &n, s->swap_images.data());

	for (VkImage img : s->swap_images)
	{
		VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
		vci.image = img;
		vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
		vci.format = s->surface_format.format;
		vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
		VkImageView view;
		Check(vkCreateImageView(s->device, &vci, nullptr, &view), "vkCreateImageView");
		s->swap_views.push_back(view);

		VkSemaphoreCreateInfo semci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
		VkSemaphore sem;
		Check(vkCreateSemaphore(s->device, &semci, nullptr, &sem), "vkCreateSemaphore");
		s->swap_drawn.push_back(sem);
	}
}

void RecreateSwapchain(RtxBackend *s)
{
	vkDeviceWaitIdle(s->device);
	DestroySwapchain(s);
	CreateSwapchain(s);
}

// --------------------------------------------------------------- overlay

uint32_t FindMemoryType(const RtxBackend *s, uint32_t bits, VkMemoryPropertyFlags want)
{
	VkPhysicalDeviceMemoryProperties mp;
	vkGetPhysicalDeviceMemoryProperties(s->gpu, &mp);
	for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
		if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & want) == want)
			return i;
	Throw("no suitable memory type");
}

void CreateOverlay(RtxBackend *s)
{
	const VkDeviceSize bytes = (VkDeviceSize)s->width * s->height * 4;

	VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
	ici.imageType = VK_IMAGE_TYPE_2D;
	ici.format = VK_FORMAT_R8G8B8A8_UNORM;
	ici.extent = {(uint32_t)s->width, (uint32_t)s->height, 1};
	ici.mipLevels = 1;
	ici.arrayLayers = 1;
	ici.samples = VK_SAMPLE_COUNT_1_BIT;
	ici.tiling = VK_IMAGE_TILING_OPTIMAL;
	ici.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	Check(vkCreateImage(s->device, &ici, nullptr, &s->ov_image), "vkCreateImage");

	VkMemoryRequirements mr;
	vkGetImageMemoryRequirements(s->device, s->ov_image, &mr);
	VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
	mai.allocationSize = mr.size;
	mai.memoryTypeIndex = FindMemoryType(s, mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	Check(vkAllocateMemory(s->device, &mai, nullptr, &s->ov_memory), "vkAllocateMemory");
	Check(vkBindImageMemory(s->device, s->ov_image, s->ov_memory, 0), "vkBindImageMemory");

	VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
	vci.image = s->ov_image;
	vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
	vci.format = VK_FORMAT_R8G8B8A8_UNORM;
	vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
	Check(vkCreateImageView(s->device, &vci, nullptr, &s->ov_view), "vkCreateImageView");

	VkSamplerCreateInfo sci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
	sci.magFilter = VK_FILTER_NEAREST;
	sci.minFilter = VK_FILTER_NEAREST;
	sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	sci.addressModeU = sci.addressModeV = sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	Check(vkCreateSampler(s->device, &sci, nullptr, &s->ov_sampler), "vkCreateSampler");

	VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
	bci.size = bytes;
	bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	Check(vkCreateBuffer(s->device, &bci, nullptr, &s->staging), "vkCreateBuffer");

	vkGetBufferMemoryRequirements(s->device, s->staging, &mr);
	mai.allocationSize = mr.size;
	mai.memoryTypeIndex = FindMemoryType(s, mr.memoryTypeBits,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	Check(vkAllocateMemory(s->device, &mai, nullptr, &s->staging_memory), "vkAllocateMemory");
	Check(vkBindBufferMemory(s->device, s->staging, s->staging_memory, 0), "vkBindBufferMemory");
	Check(vkMapMemory(s->device, s->staging_memory, 0, bytes, 0, &s->staging_ptr), "vkMapMemory");
}

// -------------------------------------------------------------- pipeline

void ShowTracePicture(RtxBackend *s);

VkShaderModule CreateShader(RtxBackend *s, const uint32_t *code, size_t bytes)
{
	VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
	ci.codeSize = bytes;
	ci.pCode = code;
	VkShaderModule m;
	Check(vkCreateShaderModule(s->device, &ci, nullptr, &m), "vkCreateShaderModule");
	return m;
}

void CreatePipeline(RtxBackend *s, VkFormat color_format)
{
	// 0: the overlay, 1: the traced picture
	VkDescriptorSetLayoutBinding binding[2]{};
	for (uint32_t i = 0; i < 2; i++)
	{
		binding[i].binding = i;
		binding[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		binding[i].descriptorCount = 1;
		binding[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	}

	VkDescriptorSetLayoutCreateInfo dlci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
	dlci.bindingCount = 2;
	dlci.pBindings = binding;
	Check(vkCreateDescriptorSetLayout(s->device, &dlci, nullptr, &s->dsl), "vkCreateDescriptorSetLayout");

	VkDescriptorPoolSize psize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2};
	VkDescriptorPoolCreateInfo dpci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
	dpci.maxSets = 1;
	dpci.poolSizeCount = 1;
	dpci.pPoolSizes = &psize;
	Check(vkCreateDescriptorPool(s->device, &dpci, nullptr, &s->dpool), "vkCreateDescriptorPool");

	VkDescriptorSetAllocateInfo dsai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
	dsai.descriptorPool = s->dpool;
	dsai.descriptorSetCount = 1;
	dsai.pSetLayouts = &s->dsl;
	Check(vkAllocateDescriptorSets(s->device, &dsai, &s->dset), "vkAllocateDescriptorSets");

	VkDescriptorImageInfo dii{s->ov_sampler, s->ov_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
	VkWriteDescriptorSet wds{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
	wds.dstSet = s->dset;
	wds.dstBinding = 0;
	wds.descriptorCount = 1;
	wds.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	wds.pImageInfo = &dii;
	vkUpdateDescriptorSets(s->device, 1, &wds, 0, nullptr);
	ShowTracePicture(s);

	VkPushConstantRange pcr{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Push)};
	VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
	plci.setLayoutCount = 1;
	plci.pSetLayouts = &s->dsl;
	plci.pushConstantRangeCount = 1;
	plci.pPushConstantRanges = &pcr;
	Check(vkCreatePipelineLayout(s->device, &plci, nullptr, &s->playout), "vkCreatePipelineLayout");

	VkShaderModule vs = CreateShader(s, blit_vert_spv, sizeof(blit_vert_spv));
	VkShaderModule fs = VK_NULL_HANDLE;
	VkResult result;
	try
	{
		fs = CreateShader(s, blit_frag_spv, sizeof(blit_frag_spv));

		VkPipelineShaderStageCreateInfo stages[2]{};
		stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
		stages[0].module = vs;
		stages[0].pName = "main";
		stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		stages[1].module = fs;
		stages[1].pName = "main";

		VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
		VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
		ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
		vp.viewportCount = 1;
		vp.scissorCount = 1;
		VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
		rs.polygonMode = VK_POLYGON_MODE_FILL;
		rs.cullMode = VK_CULL_MODE_NONE;
		rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		rs.lineWidth = 1.0f;
		VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
		ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
		VkPipelineColorBlendAttachmentState att{};
		att.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
			VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
		cb.attachmentCount = 1;
		cb.pAttachments = &att;
		const VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
		VkPipelineDynamicStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
		ds.dynamicStateCount = 2;
		ds.pDynamicStates = dyn;

		VkPipelineRenderingCreateInfo rci{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
		rci.colorAttachmentCount = 1;
		rci.pColorAttachmentFormats = &color_format;

		VkGraphicsPipelineCreateInfo gpci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
		gpci.pNext = &rci;
		gpci.stageCount = 2;
		gpci.pStages = stages;
		gpci.pVertexInputState = &vi;
		gpci.pInputAssemblyState = &ia;
		gpci.pViewportState = &vp;
		gpci.pRasterizationState = &rs;
		gpci.pMultisampleState = &ms;
		gpci.pColorBlendState = &cb;
		gpci.pDynamicState = &ds;
		gpci.layout = s->playout;
		result = vkCreateGraphicsPipelines(s->device, VK_NULL_HANDLE, 1, &gpci, nullptr, &s->pipeline);
	}
	catch (...)
	{
		vkDestroyShaderModule(s->device, vs, nullptr);
		throw;
	}
	vkDestroyShaderModule(s->device, vs, nullptr);
	vkDestroyShaderModule(s->device, fs, nullptr);
	Check(result, "vkCreateGraphicsPipelines");
}

// ----------------------------------------------------------------- frame

void Barrier(VkCommandBuffer cmd, VkImage image, VkImageLayout from, VkImageLayout to,
	VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access,
	VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access)
{
	VkImageMemoryBarrier2 b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
	b.srcStageMask = src_stage;
	b.srcAccessMask = src_access;
	b.dstStageMask = dst_stage;
	b.dstAccessMask = dst_access;
	b.oldLayout = from;
	b.newLayout = to;
	b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	b.image = image;
	b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

	VkDependencyInfo di{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
	di.imageMemoryBarrierCount = 1;
	di.pImageMemoryBarriers = &b;
	vkCmdPipelineBarrier2(cmd, &di);
}

// ------------------------------------------------------------ validation

bool HasValidationLayer()
{
	uint32_t n = 0;
	vkEnumerateInstanceLayerProperties(&n, nullptr);
	std::vector<VkLayerProperties> layers(n);
	vkEnumerateInstanceLayerProperties(&n, layers.data());
	for (const VkLayerProperties &l : layers)
		if (!strcmp(l.layerName, "VK_LAYER_KHRONOS_validation"))
			return true;
	return false;
}

VKAPI_ATTR VkBool32 VKAPI_CALL OnMessage(VkDebugUtilsMessageSeverityFlagBitsEXT, VkDebugUtilsMessageTypeFlagsEXT,
	const VkDebugUtilsMessengerCallbackDataEXT *data, void *user)
{
	Logf(static_cast<const RtxBackend *>(user), "Vulkan: %s\n", data->pMessage);
	return VK_FALSE;
}

void CreateMessenger(RtxBackend *s)
{
	auto create = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(s->instance, "vkCreateDebugUtilsMessengerEXT");
	if (!create)
		return;
	VkDebugUtilsMessengerCreateInfoEXT ci{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
	ci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
	ci.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT;
	ci.pfnUserCallback = OnMessage;
	ci.pUserData = s;
	create(s->instance, &ci, nullptr, &s->messenger);
	Logf(s, "RTX path tracer: Vulkan validation is on\n");
}

// ---------------------------------------------------------------- memory

// commands run at once, outside a frame; nothing is left pending afterwards
VkCommandBuffer BeginOnce(RtxBackend *s)
{
	vkQueueWaitIdle(s->queue);
	vkResetCommandBuffer(s->cmd_once, 0);
	VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
	bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	Check(vkBeginCommandBuffer(s->cmd_once, &bi), "vkBeginCommandBuffer");
	return s->cmd_once;
}

void EndOnce(RtxBackend *s)
{
	Check(vkEndCommandBuffer(s->cmd_once), "vkEndCommandBuffer");
	VkCommandBufferSubmitInfo cbi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
	cbi.commandBuffer = s->cmd_once;
	VkSubmitInfo2 si{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
	si.commandBufferInfoCount = 1;
	si.pCommandBufferInfos = &cbi;
	Check(vkQueueSubmit2(s->queue, 1, &si, VK_NULL_HANDLE), "vkQueueSubmit2");
	Check(vkQueueWaitIdle(s->queue), "vkQueueWaitIdle");
}

bool TryMemoryType(const RtxBackend *s, uint32_t bits, VkMemoryPropertyFlags want, uint32_t &type)
{
	VkPhysicalDeviceMemoryProperties mp;
	vkGetPhysicalDeviceMemoryProperties(s->gpu, &mp);
	for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
	{
		if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & want) == want)
		{
			type = i;
			return true;
		}
	}
	return false;
}

void FreeBuffer(RtxBackend *s, Buffer &b)
{
	if (b.buffer)
		vkDestroyBuffer(s->device, b.buffer, nullptr);
	if (b.memory)
		vkFreeMemory(s->device, b.memory, nullptr);		// unmaps it too
	b = Buffer();
}

// A buffer the shaders and the acceleration structure builds can address.
// `mapped` ones are written from here, and sit in the card's own memory
// where it can be reached from here at all.
Buffer MakeBuffer(RtxBackend *s, VkDeviceSize size, VkBufferUsageFlags usage, bool mapped)
{
	Buffer b;
	try
	{
		b.size = std::max<VkDeviceSize>(size, 64);
		VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
		bci.size = b.size;
		bci.usage = usage | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
		bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		Check(vkCreateBuffer(s->device, &bci, nullptr, &b.buffer), "vkCreateBuffer");

		VkMemoryRequirements mr;
		vkGetBufferMemoryRequirements(s->device, b.buffer, &mr);
		const VkMemoryPropertyFlags host = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
		uint32_t type = 0;
		if (mapped)
		{
			if (!TryMemoryType(s, mr.memoryTypeBits, host | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, type)
				&& !TryMemoryType(s, mr.memoryTypeBits, host, type))
				Throw("no memory the host can write");
		}
		else
			type = FindMemoryType(s, mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

		VkMemoryAllocateFlagsInfo flags{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO};
		flags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
		VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
		mai.pNext = &flags;
		mai.allocationSize = mr.size;
		mai.memoryTypeIndex = type;
		Check(vkAllocateMemory(s->device, &mai, nullptr, &b.memory), "vkAllocateMemory");
		Check(vkBindBufferMemory(s->device, b.buffer, b.memory, 0), "vkBindBufferMemory");
		if (mapped)
			Check(vkMapMemory(s->device, b.memory, 0, VK_WHOLE_SIZE, 0, &b.ptr), "vkMapMemory");

		VkBufferDeviceAddressInfo ai{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
		ai.buffer = b.buffer;
		b.address = vkGetBufferDeviceAddress(s->device, &ai);
	}
	catch (...)
	{
		FreeBuffer(s, b);
		throw;
	}
	return b;
}

// -------------------------------------------------------------- textures

void FreeTexture(RtxBackend *s, Texture &t)
{
	if (t.view)
		vkDestroyImageView(s->device, t.view, nullptr);
	if (t.image)
		vkDestroyImage(s->device, t.image, nullptr);
	if (t.memory)
		vkFreeMemory(s->device, t.memory, nullptr);
	t = Texture();
}

// width * height pixels of R,G,B,A bytes, onto the card
Texture MakeTexture(RtxBackend *s, int width, int height, const uint32_t *pixels)
{
	Texture t;
	Buffer staging;
	try
	{
		t.width = width;
		t.height = height;
		const VkDeviceSize bytes = (VkDeviceSize)width * height * 4;

		VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
		ici.imageType = VK_IMAGE_TYPE_2D;
		// as stored: the shaders undo the display encoding where it is colour
		ici.format = VK_FORMAT_R8G8B8A8_UNORM;
		ici.extent = {(uint32_t)width, (uint32_t)height, 1};
		ici.mipLevels = 1;
		ici.arrayLayers = 1;
		ici.samples = VK_SAMPLE_COUNT_1_BIT;
		ici.tiling = VK_IMAGE_TILING_OPTIMAL;
		ici.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		Check(vkCreateImage(s->device, &ici, nullptr, &t.image), "vkCreateImage");

		VkMemoryRequirements mr;
		vkGetImageMemoryRequirements(s->device, t.image, &mr);
		VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
		mai.allocationSize = mr.size;
		mai.memoryTypeIndex = FindMemoryType(s, mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		Check(vkAllocateMemory(s->device, &mai, nullptr, &t.memory), "vkAllocateMemory");
		Check(vkBindImageMemory(s->device, t.image, t.memory, 0), "vkBindImageMemory");

		VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
		vci.image = t.image;
		vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
		vci.format = VK_FORMAT_R8G8B8A8_UNORM;
		vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
		Check(vkCreateImageView(s->device, &vci, nullptr, &t.view), "vkCreateImageView");

		staging = MakeBuffer(s, bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true);
		memcpy(staging.ptr, pixels, (size_t)bytes);

		VkCommandBuffer cmd = BeginOnce(s);
		Barrier(cmd, t.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
			VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
		VkBufferImageCopy region{};
		region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
		region.imageExtent = {(uint32_t)width, (uint32_t)height, 1};
		vkCmdCopyBufferToImage(cmd, staging.buffer, t.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
		Barrier(cmd, t.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
		EndOnce(s);
		FreeBuffer(s, staging);
	}
	catch (...)
	{
		FreeBuffer(s, staging);
		FreeTexture(s, t);
		throw;
	}
	return t;
}

// what the shaders find in a slot: its texture, or the blank one
void ShowTexture(RtxBackend *s, int slot)
{
	const Texture &t = s->textures[slot].view ? s->textures[slot] : s->blank;
	VkDescriptorImageInfo dii{s->tex_sampler, t.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
	VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
	w.dstSet = s->trace_dset;
	w.dstBinding = 8;
	w.dstArrayElement = (uint32_t)slot;
	w.descriptorCount = 1;
	w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	w.pImageInfo = &dii;
	vkUpdateDescriptorSets(s->device, 1, &w, 0, nullptr);
}

// Puts a texture in a free slot. Returns the slot, or -1 if there is none
// left or nothing to put there.
int AddTexture(RtxBackend *s, const pt_texture_t *in)
{
	if (!in || !in->pixels || in->width <= 0 || in->height <= 0)
		return -1;
	for (uint32_t slot = 0; slot < kMaxTextures; slot++)
	{
		if (s->textures[slot].view)
			continue;
		s->textures[slot] = MakeTexture(s, in->width, in->height, in->pixels);	// leaves the queue idle
		ShowTexture(s, (int)slot);
		return (int)slot;
	}
	return -1;
}

void RemoveTexture(RtxBackend *s, int slot)
{
	if (slot < 0 || slot >= (int)kMaxTextures || !s->textures[slot].view)
		return;
	vkQueueWaitIdle(s->queue);
	FreeTexture(s, s->textures[slot]);
	ShowTexture(s, slot);
}

// ------------------------------------------------ acceleration structures

void FreeAccel(RtxBackend *s, Accel &a)
{
	if (a.as)
		s->DestroyAccelerationStructure(s->device, a.as, nullptr);
	FreeBuffer(s, a.storage);
	FreeBuffer(s, a.scratch);
	a = Accel();
}

// triangles given as three corners each, one after another
VkAccelerationStructureGeometryKHR TriangleGeometry(VkDeviceAddress corners, uint32_t num_tris)
{
	VkAccelerationStructureGeometryKHR g{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
	g.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
	g.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
	g.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
	g.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
	g.geometry.triangles.vertexData.deviceAddress = corners;
	g.geometry.triangles.vertexStride = 12;
	g.geometry.triangles.maxVertex = num_tris ? num_tris * 3 - 1 : 0;
	g.geometry.triangles.indexType = VK_INDEX_TYPE_NONE_KHR;
	return g;
}

VkAccelerationStructureGeometryKHR InstanceGeometry(VkDeviceAddress instances)
{
	VkAccelerationStructureGeometryKHR g{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
	g.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
	g.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
	g.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
	g.geometry.instances.data.deviceAddress = instances;
	return g;
}

// room for a structure over up to max_count of what the geometry describes,
// and for the working space a build of it needs
void MakeAccel(RtxBackend *s, Accel &a, VkAccelerationStructureTypeKHR type,
	const VkAccelerationStructureGeometryKHR &geometry, uint32_t max_count, VkBuildAccelerationStructureFlagsKHR flags)
{
	FreeAccel(s, a);

	VkAccelerationStructureBuildGeometryInfoKHR info{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
	info.type = type;
	info.flags = flags;
	info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
	info.geometryCount = 1;
	info.pGeometries = &geometry;

	VkAccelerationStructureBuildSizesInfoKHR sizes{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
	s->GetAccelerationStructureBuildSizes(s->device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &info, &max_count, &sizes);

	a.storage = MakeBuffer(s, sizes.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR, false);
	VkAccelerationStructureCreateInfoKHR ci{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
	ci.buffer = a.storage.buffer;
	ci.size = sizes.accelerationStructureSize;
	ci.type = type;
	Check(s->CreateAccelerationStructure(s->device, &ci, nullptr, &a.as), "vkCreateAccelerationStructureKHR");

	a.scratch = MakeBuffer(s, sizes.buildScratchSize + s->scratch_align, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false);
	a.scratch_address = (a.scratch.address + s->scratch_align - 1) & ~(VkDeviceAddress)(s->scratch_align - 1);

	VkAccelerationStructureDeviceAddressInfoKHR ai{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};
	ai.accelerationStructure = a.as;
	a.address = s->GetAccelerationStructureDeviceAddress(s->device, &ai);
}

void BuildAccel(RtxBackend *s, VkCommandBuffer cmd, const Accel &a, VkAccelerationStructureTypeKHR type,
	const VkAccelerationStructureGeometryKHR &geometry, uint32_t count, VkBuildAccelerationStructureFlagsKHR flags)
{
	VkAccelerationStructureBuildGeometryInfoKHR info{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
	info.type = type;
	info.flags = flags;
	info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
	info.dstAccelerationStructure = a.as;
	info.geometryCount = 1;
	info.pGeometries = &geometry;
	info.scratchData.deviceAddress = a.scratch_address;

	VkAccelerationStructureBuildRangeInfoKHR range{};
	range.primitiveCount = count;
	const VkAccelerationStructureBuildRangeInfoKHR *ranges = &range;
	s->CmdBuildAccelerationStructures(cmd, 1, &info, &ranges);
}

// what was just built may be read by what comes next
void AfterBuild(VkCommandBuffer cmd, VkPipelineStageFlags2 next_stage)
{
	VkMemoryBarrier2 b{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
	b.srcStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
	b.srcAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
	b.dstStageMask = next_stage;
	b.dstAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR;
	VkDependencyInfo di{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
	di.memoryBarrierCount = 1;
	di.pMemoryBarriers = &b;
	vkCmdPipelineBarrier2(cmd, &di);
}

// -------------------------------------------------------------- geometry

const VkBuildAccelerationStructureFlagsKHR kWorldBuild = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
const VkBuildAccelerationStructureFlagsKHR kFrameBuild = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;

void FreeGeometry(RtxBackend *s, Geometry &g)
{
	FreeBuffer(s, g.corners);
	FreeBuffer(s, g.tris);
	FreeBuffer(s, g.materials);
	FreeAccel(s, g.blas);
	g = Geometry();
}

// Makes sure there is room for so many triangles and materials. Returns
// true if any buffer is a new one, which the shaders then have to be shown.
bool Reserve(RtxBackend *s, Geometry &g, uint32_t tris, uint32_t materials, VkBuildAccelerationStructureFlagsKHR build, bool grows)
{
	bool changed = false;

	if (!g.corners.buffer || tris > g.room_tris)
	{
		vkQueueWaitIdle(s->queue);
		FreeBuffer(s, g.corners);
		FreeBuffer(s, g.tris);
		// what changes every frame is given room to spare, so that it is not
		// made again for every triangle more
		g.room_tris = grows ? std::max(tris + tris / 2, 4096u) : std::max(tris, 1u);
		g.corners = MakeBuffer(s, (VkDeviceSize)g.room_tris * 36,
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR, true);
		g.tris = MakeBuffer(s, (VkDeviceSize)g.room_tris * sizeof(GpuTri), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true);
		MakeAccel(s, g.blas, VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
			TriangleGeometry(g.corners.address, g.room_tris), g.room_tris, build);
		changed = true;
	}
	if (!g.materials.buffer || materials > g.room_materials)
	{
		vkQueueWaitIdle(s->queue);
		FreeBuffer(s, g.materials);
		g.room_materials = grows ? std::max(materials * 2, 256u) : std::max(materials, 1u);
		g.materials = MakeBuffer(s, (VkDeviceSize)g.room_materials * sizeof(GpuMaterial), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true);
		changed = true;
	}
	return changed;
}

// shows the tracing shader the buffers of the map and of the frame
void ShowGeometry(RtxBackend *s)
{
	const Buffer *buffers[6] = {&s->world.corners, &s->world.tris, &s->world.materials,
		&s->frame.corners, &s->frame.tris, &s->frame.materials};
	VkDescriptorBufferInfo info[6];
	VkWriteDescriptorSet w[6];
	uint32_t n = 0;
	for (uint32_t i = 0; i < 6; i++)
	{
		if (!buffers[i]->buffer)
			continue;
		info[n] = {buffers[i]->buffer, 0, VK_WHOLE_SIZE};
		w[n] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
		w[n].dstSet = s->trace_dset;
		w[n].dstBinding = 2 + i;
		w[n].descriptorCount = 1;
		w[n].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		w[n].pBufferInfo = &info[n];
		n++;
	}
	vkUpdateDescriptorSets(s->device, n, w, 0, nullptr);
}

void SetMaterial(const RtxBackend *s, GpuMaterial &out, const pt_material_t &in, int texture_slot)
{
	out.texture = (texture_slot >= 0 && texture_slot < (int)kMaxTextures && s->textures[texture_slot].view) ? texture_slot : -1;
	out.flags = in.flags;
	out.alpha = in.alpha;
	out.roughness = in.roughness;
	out.emission[0] = in.emission[0];
	out.emission[1] = in.emission[1];
	out.emission[2] = in.emission[2];
	out.emission[3] = 0.0f;
}

// ------------------------------------------------------ the traced picture

// the last pass shows it in the view
void ShowTracePicture(RtxBackend *s)
{
	if (!s->dset || !s->trace_view)
		return;
	VkDescriptorImageInfo dii{s->trace_sampler, s->trace_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
	VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
	w.dstSet = s->dset;
	w.dstBinding = 1;
	w.descriptorCount = 1;
	w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	w.pImageInfo = &dii;
	vkUpdateDescriptorSets(s->device, 1, &w, 0, nullptr);
}

void FreeTracePicture(RtxBackend *s)
{
	if (s->trace_view) vkDestroyImageView(s->device, s->trace_view, nullptr);
	if (s->trace_image) vkDestroyImage(s->device, s->trace_image, nullptr);
	if (s->trace_memory) vkFreeMemory(s->device, s->trace_memory, nullptr);
	s->trace_view = VK_NULL_HANDLE;
	s->trace_image = VK_NULL_HANDLE;
	s->trace_memory = VK_NULL_HANDLE;
}

void MakeTracePicture(RtxBackend *s, int width, int height)
{
	vkQueueWaitIdle(s->queue);
	FreeTracePicture(s);
	s->trace_width = width;
	s->trace_height = height;

	VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
	ici.imageType = VK_IMAGE_TYPE_2D;
	ici.format = VK_FORMAT_R8G8B8A8_UNORM;
	ici.extent = {(uint32_t)width, (uint32_t)height, 1};
	ici.mipLevels = 1;
	ici.arrayLayers = 1;
	ici.samples = VK_SAMPLE_COUNT_1_BIT;
	ici.tiling = VK_IMAGE_TILING_OPTIMAL;
	ici.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	Check(vkCreateImage(s->device, &ici, nullptr, &s->trace_image), "vkCreateImage");

	VkMemoryRequirements mr;
	vkGetImageMemoryRequirements(s->device, s->trace_image, &mr);
	VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
	mai.allocationSize = mr.size;
	mai.memoryTypeIndex = FindMemoryType(s, mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	Check(vkAllocateMemory(s->device, &mai, nullptr, &s->trace_memory), "vkAllocateMemory");
	Check(vkBindImageMemory(s->device, s->trace_image, s->trace_memory, 0), "vkBindImageMemory");

	VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
	vci.image = s->trace_image;
	vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
	vci.format = VK_FORMAT_R8G8B8A8_UNORM;
	vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
	Check(vkCreateImageView(s->device, &vci, nullptr, &s->trace_view), "vkCreateImageView");

	// between frames it is always ready to be shown
	VkCommandBuffer cmd = BeginOnce(s);
	Barrier(cmd, s->trace_image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
		VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
	EndOnce(s);

	VkDescriptorImageInfo dii{VK_NULL_HANDLE, s->trace_view, VK_IMAGE_LAYOUT_GENERAL};
	VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
	w.dstSet = s->trace_dset;
	w.dstBinding = 1;
	w.descriptorCount = 1;
	w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	w.pImageInfo = &dii;
	vkUpdateDescriptorSets(s->device, 1, &w, 0, nullptr);
	ShowTracePicture(s);
}

// ------------------------------------------------------------------ scene

// everything the tracer needs that does not depend on the map
void CreateScene(RtxBackend *s)
{
	VkSamplerCreateInfo sci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
	sci.magFilter = VK_FILTER_NEAREST;
	sci.minFilter = VK_FILTER_NEAREST;
	sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	sci.addressModeU = sci.addressModeV = sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	Check(vkCreateSampler(s->device, &sci, nullptr, &s->tex_sampler), "vkCreateSampler");

	sci.magFilter = VK_FILTER_LINEAR;
	sci.minFilter = VK_FILTER_LINEAR;
	sci.addressModeU = sci.addressModeV = sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	Check(vkCreateSampler(s->device, &sci, nullptr, &s->trace_sampler), "vkCreateSampler");

	// 0 the scene, 1 the picture, 2-7 the map's and the frame's buffers, 8 the textures
	VkDescriptorSetLayoutBinding bind[9]{};
	for (uint32_t i = 0; i < 9; i++)
	{
		bind[i].binding = i;
		bind[i].descriptorCount = 1;
		bind[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		bind[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	}
	bind[0].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
	bind[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	bind[8].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	bind[8].descriptorCount = kMaxTextures;

	VkDescriptorSetLayoutCreateInfo dlci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
	dlci.bindingCount = 9;
	dlci.pBindings = bind;
	Check(vkCreateDescriptorSetLayout(s->device, &dlci, nullptr, &s->trace_dsl), "vkCreateDescriptorSetLayout");

	const VkDescriptorPoolSize sizes[] = {
		{VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1},
		{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1},
		{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 6},
		{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kMaxTextures},
	};
	VkDescriptorPoolCreateInfo dpci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
	dpci.maxSets = 1;
	dpci.poolSizeCount = 4;
	dpci.pPoolSizes = sizes;
	Check(vkCreateDescriptorPool(s->device, &dpci, nullptr, &s->trace_dpool), "vkCreateDescriptorPool");

	VkDescriptorSetAllocateInfo dsai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
	dsai.descriptorPool = s->trace_dpool;
	dsai.descriptorSetCount = 1;
	dsai.pSetLayouts = &s->trace_dsl;
	Check(vkAllocateDescriptorSets(s->device, &dsai, &s->trace_dset), "vkAllocateDescriptorSets");

	VkPushConstantRange pcr{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(TracePush)};
	VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
	plci.setLayoutCount = 1;
	plci.pSetLayouts = &s->trace_dsl;
	plci.pushConstantRangeCount = 1;
	plci.pPushConstantRanges = &pcr;
	Check(vkCreatePipelineLayout(s->device, &plci, nullptr, &s->trace_playout), "vkCreatePipelineLayout");

	VkShaderModule module = CreateShader(s, trace_comp_spv, sizeof(trace_comp_spv));
	VkComputePipelineCreateInfo cpci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
	cpci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	cpci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	cpci.stage.module = module;
	cpci.stage.pName = "main";
	cpci.layout = s->trace_playout;
	const VkResult result = vkCreateComputePipelines(s->device, VK_NULL_HANDLE, 1, &cpci, nullptr, &s->trace_pipeline);
	vkDestroyShaderModule(s->device, module, nullptr);
	Check(result, "vkCreateComputePipelines");

	// every texture slot shows something from the start
	const uint32_t white = 0xffffffffu;
	s->blank = MakeTexture(s, 1, 1, &white);
	{
		std::vector<VkDescriptorImageInfo> all(kMaxTextures,
			VkDescriptorImageInfo{s->tex_sampler, s->blank.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
		VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
		w.dstSet = s->trace_dset;
		w.dstBinding = 8;
		w.descriptorCount = kMaxTextures;
		w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		w.pImageInfo = all.data();
		vkUpdateDescriptorSets(s->device, 1, &w, 0, nullptr);
	}

	// the two instances: the map and what moves
	s->instances = MakeBuffer(s, 2 * sizeof(VkAccelerationStructureInstanceKHR),
		VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR, true);
	MakeAccel(s, s->tlas, VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
		InstanceGeometry(s->instances.address), 2, kFrameBuild);
	{
		VkWriteDescriptorSetAccelerationStructureKHR as{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR};
		as.accelerationStructureCount = 1;
		as.pAccelerationStructures = &s->tlas.as;
		VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
		w.pNext = &as;
		w.dstSet = s->trace_dset;
		w.dstBinding = 0;
		w.descriptorCount = 1;
		w.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
		vkUpdateDescriptorSets(s->device, 1, &w, 0, nullptr);
	}

	// something in every binding before there is a map
	Reserve(s, s->world, 1, 1, kWorldBuild, false);
	Reserve(s, s->frame, 1, 1, kFrameBuild, true);
	ShowGeometry(s);
	MakeTracePicture(s, s->width, s->height);
}

void DestroyScene(RtxBackend *s)
{
	FreeGeometry(s, s->world);
	FreeGeometry(s, s->frame);
	FreeAccel(s, s->tlas);
	FreeBuffer(s, s->instances);
	for (Texture &t : s->textures)
		FreeTexture(s, t);
	FreeTexture(s, s->blank);
	FreeTracePicture(s);
	if (s->trace_pipeline) vkDestroyPipeline(s->device, s->trace_pipeline, nullptr);
	if (s->trace_playout) vkDestroyPipelineLayout(s->device, s->trace_playout, nullptr);
	if (s->trace_dpool) vkDestroyDescriptorPool(s->device, s->trace_dpool, nullptr);
	if (s->trace_dsl) vkDestroyDescriptorSetLayout(s->device, s->trace_dsl, nullptr);
	if (s->trace_sampler) vkDestroySampler(s->device, s->trace_sampler, nullptr);
	if (s->tex_sampler) vkDestroySampler(s->device, s->tex_sampler, nullptr);
}

void LoadWorldNow(RtxBackend *s, const pt_world_t *in)
{
	vkQueueWaitIdle(s->queue);

	// the map before this one
	s->world_loaded = false;
	for (int slot : s->world_textures)
		if (slot >= 0)
		{
			FreeTexture(s, s->textures[slot]);
			ShowTexture(s, slot);
		}
	s->world_textures.clear();
	for (int32_t &face : s->sky)
		face = -1;

	if (!in || in->num_triangles <= 0)
		return;

	s->world_textures.assign(in->num_textures, -1);
	for (int i = 0; i < in->num_textures; i++)
		s->world_textures[i] = AddTexture(s, &in->textures[i]);
	const auto slot = [&](int texture)
	{
		return (texture >= 0 && texture < in->num_textures) ? s->world_textures[texture] : -1;
	};

	Reserve(s, s->world, (uint32_t)in->num_triangles, (uint32_t)std::max(in->num_materials, 1), kWorldBuild, false);
	ShowGeometry(s);

	float *corners = static_cast<float *>(s->world.corners.ptr);
	GpuTri *tris = static_cast<GpuTri *>(s->world.tris.ptr);
	for (int t = 0; t < in->num_triangles; t++)
	{
		tris[t].pad = 0;
		tris[t].material = (in->tri_materials && (int)in->tri_materials[t] < in->num_materials) ? in->tri_materials[t] : 0;
		for (int k = 0; k < 3; k++)
		{
			const uint32_t v = in->indices[t * 3 + k];
			memcpy(&corners[t * 9 + k * 3], &in->positions[v * 3], 12);
			tris[t].uv[k * 2] = in->uvs ? in->uvs[v * 2] : 0.0f;
			tris[t].uv[k * 2 + 1] = in->uvs ? in->uvs[v * 2 + 1] : 0.0f;
		}
	}

	GpuMaterial *materials = static_cast<GpuMaterial *>(s->world.materials.ptr);
	if (in->num_materials <= 0)
		materials[0] = GpuMaterial{-1, 0, 1.0f, 1.0f, {0, 0, 0, 0}};
	for (int i = 0; i < in->num_materials; i++)
		SetMaterial(s, materials[i], in->materials[i], slot(in->materials[i].texture));

	s->world.num_tris = (uint32_t)in->num_triangles;
	VkCommandBuffer cmd = BeginOnce(s);
	BuildAccel(s, cmd, s->world.blas, VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
		TriangleGeometry(s->world.corners.address, s->world.num_tris), s->world.num_tris, kWorldBuild);
	EndOnce(s);

	for (int f = 0; f < 6; f++)
		s->sky[f] = slot(in->sky_textures[f]);
	s->world_loaded = true;
	Logf(s, "RTX path tracer: %d triangles, %d materials, %d textures on the card\n",
		in->num_triangles, in->num_materials, in->num_textures);
}

// what moves this frame, and the view: all the tracer needs to be told
void RenderViewNow(RtxBackend *s, const pt_view_t *view)
{
	s->view = *view;
	s->has_view = true;
	s->trace_ready = false;
	s->stats[0] = 0;
	if (!s->world_loaded || view->width <= 0 || view->height <= 0)
		return;

	// the card may still be reading last frame's buffers
	vkWaitForFences(s->device, 1, &s->fence, VK_TRUE, UINT64_MAX);

	const float scale = view->scale < 0.05f ? 0.05f : (view->scale > 1.0f ? 1.0f : view->scale);
	const int rw = std::max(1, (int)(view->width * scale + 0.5f));
	const int rh = std::max(1, (int)(view->height * scale + 0.5f));
	if (rw != s->trace_width || rh != s->trace_height)
		MakeTracePicture(s, rw, rh);

	const pt_scene_t *scene = view->scene;
	const uint32_t n = (scene && scene->positions && scene->num_triangles > 0) ? (uint32_t)scene->num_triangles : 0;
	const uint32_t num_materials = (scene && scene->num_materials > 0) ? (uint32_t)scene->num_materials : 0;
	if (Reserve(s, s->frame, n, std::max(num_materials, 1u), kFrameBuild, true))
		ShowGeometry(s);

	if (n)
	{
		memcpy(s->frame.corners.ptr, scene->positions, (size_t)n * 36);
		GpuTri *tris = static_cast<GpuTri *>(s->frame.tris.ptr);
		for (uint32_t t = 0; t < n; t++)
		{
			if (scene->uvs)
				memcpy(tris[t].uv, &scene->uvs[t * 6], 24);
			else
				memset(tris[t].uv, 0, 24);
			tris[t].material = (scene->tri_materials && scene->tri_materials[t] < num_materials) ? scene->tri_materials[t] : 0;
			tris[t].pad = 0;
		}
	}
	GpuMaterial *materials = static_cast<GpuMaterial *>(s->frame.materials.ptr);
	materials[0] = GpuMaterial{-1, 0, 1.0f, 1.0f, {0, 0, 0, 0}};
	for (uint32_t i = 0; i < num_materials; i++)
		SetMaterial(s, materials[i], scene->materials[i], scene->materials[i].texture);	// already a slot
	s->frame.num_tris = n;

	VkAccelerationStructureInstanceKHR *inst = static_cast<VkAccelerationStructureInstanceKHR *>(s->instances.ptr);
	memset(inst, 0, 2 * sizeof(*inst));
	for (int i = 0; i < 2; i++)
	{
		inst[i].transform.matrix[0][0] = inst[i].transform.matrix[1][1] = inst[i].transform.matrix[2][2] = 1.0f;
		inst[i].instanceCustomIndex = (uint32_t)i;		// the shader tells them apart by this
		inst[i].mask = 0xff;
		inst[i].flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
		inst[i].accelerationStructureReference = i == 0 ? s->world.blas.address : s->frame.blas.address;
	}

	s->trace_ready = true;
	snprintf(s->stats, sizeof(s->stats), "%dx%d|%u + %u triangles|no lighting yet", rw, rh, s->world.num_tris, n);
}

// traces the view into the picture; part of the frame's commands
void RecordTrace(RtxBackend *s, VkCommandBuffer cmd)
{
	if (s->frame.num_tris)
	{
		BuildAccel(s, cmd, s->frame.blas, VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
			TriangleGeometry(s->frame.corners.address, s->frame.num_tris), s->frame.num_tris, kFrameBuild);
		AfterBuild(cmd, VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR);
	}
	BuildAccel(s, cmd, s->tlas, VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
		InstanceGeometry(s->instances.address), s->frame.num_tris ? 2 : 1, kFrameBuild);
	AfterBuild(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);

	// all of it is written afresh, so what it held need not be kept
	Barrier(cmd, s->trace_image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
		VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_NONE,
		VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);

	const pt_view_t &v = s->view;
	TracePush push{};
	for (int i = 0; i < 3; i++)
	{
		push.origin[i] = v.origin[i];
		push.forward[i] = v.forward[i];
		push.right[i] = v.right[i];
		push.up[i] = v.up[i];
	}
	push.origin[3] = std::tan(v.fov_x * 0.5f * 3.14159265f / 180.0f);
	push.forward[3] = std::tan(v.fov_y * 0.5f * 3.14159265f / 180.0f);
	for (int i = 0; i < 8; i++)
		push.sky[i] = i < 6 ? s->sky[i] : -1;

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, s->trace_pipeline);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, s->trace_playout, 0, 1, &s->trace_dset, 0, nullptr);
	vkCmdPushConstants(cmd, s->trace_playout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
	vkCmdDispatch(cmd, ((uint32_t)s->trace_width + 7) / 8, ((uint32_t)s->trace_height + 7) / 8, 1);

	Barrier(cmd, s->trace_image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
		VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
}

// -------------------------------------------------- the backend's entries
//
// These are called from C and must not let an exception out.

void RenderView(pt_backend_t *b, const pt_view_t *view)
{
	RtxBackend *s = Self(b);
	try
	{
		RenderViewNow(s, view);
	}
	catch (const Fail &f)
	{
		Logf(s, "RTX path tracer: %s\n", f.msg.c_str());
		s->trace_ready = false;
	}
}

void LoadWorld(pt_backend_t *b, const pt_world_t *world)
{
	RtxBackend *s = Self(b);
	try
	{
		LoadWorldNow(s, world);
	}
	catch (const Fail &f)
	{
		Logf(s, "RTX path tracer: could not load the map: %s\n", f.msg.c_str());
		s->world_loaded = false;
	}
}

const char *Stats(pt_backend_t *b)
{
	return Self(b)->stats;
}

int TextureCreate(pt_backend_t *b, const pt_texture_t *texture)
{
	RtxBackend *s = Self(b);
	try
	{
		return AddTexture(s, texture);
	}
	catch (const Fail &f)
	{
		Logf(s, "RTX path tracer: %s\n", f.msg.c_str());
		return -1;
	}
}

void TextureDestroy(pt_backend_t *b, int handle)
{
	RemoveTexture(Self(b), handle);
}

// Nothing reads the pictures that change (the wave maps of simulated water)
// yet, so there is nothing to bring up to date.
void TextureUpdate(pt_backend_t *, int, const uint32_t *)
{
}

int ReadPixels(pt_backend_t *, uint32_t *, int)
{
	return 0;
}

void Record(RtxBackend *s, uint32_t image_index)
{
	VkCommandBuffer cmd = s->cmd;
	VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
	bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	vkBeginCommandBuffer(cmd, &bi);

	// upload this frame's overlay
	Barrier(cmd, s->ov_image,
		s->ov_initialized ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		s->ov_initialized ? VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_2_NONE,
		s->ov_initialized ? VK_ACCESS_2_SHADER_SAMPLED_READ_BIT : VK_ACCESS_2_NONE,
		VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
	VkBufferImageCopy region{};
	region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
	region.imageExtent = {(uint32_t)s->width, (uint32_t)s->height, 1};
	vkCmdCopyBufferToImage(cmd, s->staging, s->ov_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
	Barrier(cmd, s->ov_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
		VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
	s->ov_initialized = true;

	if (s->trace_ready)
		RecordTrace(s, cmd);

	Barrier(cmd, s->swap_images[image_index], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_NONE,
		VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

	VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
	color.imageView = s->swap_views[image_index];
	color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	color.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;	// the triangle covers everything
	color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

	VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
	ri.renderArea = {{0, 0}, s->extent};
	ri.layerCount = 1;
	ri.colorAttachmentCount = 1;
	ri.pColorAttachments = &color;
	vkCmdBeginRendering(cmd, &ri);

	const VkViewport viewport{0.0f, 0.0f, (float)s->extent.width, (float)s->extent.height, 0.0f, 1.0f};
	const VkRect2D scissor{{0, 0}, s->extent};
	vkCmdSetViewport(cmd, 0, 1, &viewport);
	vkCmdSetScissor(cmd, 0, 1, &scissor);

	Push push{};
	push.view[0] = (float)s->view.x;
	push.view[1] = (float)s->view.y;
	push.view[2] = (float)s->view.width;
	push.view[3] = (float)s->view.height;
	push.screen[0] = (float)s->width;
	push.screen[1] = (float)s->height;
	push.has_view = s->trace_ready ? 1.0f : 0.0f;

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s->pipeline);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s->playout, 0, 1, &s->dset, 0, nullptr);
	vkCmdPushConstants(cmd, s->playout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
	vkCmdDraw(cmd, 3, 1, 0, 0);
	vkCmdEndRendering(cmd);

	Barrier(cmd, s->swap_images[image_index], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
		VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
		VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE);

	vkEndCommandBuffer(cmd);
}

void PresentFrame(RtxBackend *s, const uint32_t *overlay)
{
	if (!s->swapchain)
	{
		CreateSwapchain(s);		// window may have been minimized
		if (!s->swapchain)
			return;
	}

	vkWaitForFences(s->device, 1, &s->fence, VK_TRUE, UINT64_MAX);

	uint32_t index = 0;
	VkResult r = vkAcquireNextImageKHR(s->device, s->swapchain, UINT64_MAX, s->sem_acquire, VK_NULL_HANDLE, &index);
	if (r == VK_ERROR_OUT_OF_DATE_KHR)
	{
		RecreateSwapchain(s);
		return;
	}
	if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR)
		Check(r, "vkAcquireNextImageKHR");

	memcpy(s->staging_ptr, overlay, (size_t)s->width * s->height * 4);
	vkResetFences(s->device, 1, &s->fence);
	vkResetCommandBuffer(s->cmd, 0);
	Record(s, index);

	VkSemaphoreSubmitInfo wait{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
	wait.semaphore = s->sem_acquire;
	wait.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
	VkSemaphoreSubmitInfo signal{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
	signal.semaphore = s->swap_drawn[index];
	VkCommandBufferSubmitInfo cbi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
	cbi.commandBuffer = s->cmd;

	VkSubmitInfo2 si{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
	si.waitSemaphoreInfoCount = 1;
	si.pWaitSemaphoreInfos = &wait;
	si.commandBufferInfoCount = 1;
	si.pCommandBufferInfos = &cbi;
	si.signalSemaphoreInfoCount = 1;
	si.pSignalSemaphoreInfos = &signal;
	Check(vkQueueSubmit2(s->queue, 1, &si, s->fence), "vkQueueSubmit2");

	VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
	pi.waitSemaphoreCount = 1;
	pi.pWaitSemaphores = &s->swap_drawn[index];
	pi.swapchainCount = 1;
	pi.pSwapchains = &s->swapchain;
	pi.pImageIndices = &index;
	r = vkQueuePresentKHR(s->queue, &pi);
	if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR)
		RecreateSwapchain(s);
	else
		Check(r, "vkQueuePresentKHR");
}

void Present(pt_backend_t *b, const uint32_t *overlay)
{
	RtxBackend *s = Self(b);
	try
	{
		PresentFrame(s, overlay);
	}
	catch (const Fail &f)
	{
		// nothing useful to do mid-frame; say so and keep the game alive
		Logf(s, "RTX path tracer: %s\n", f.msg.c_str());
	}
	s->has_view = false;
	s->trace_ready = false;
}

void Destroy(pt_backend_t *b)
{
	RtxBackend *s = Self(b);
	if (s->device)
	{
		vkDeviceWaitIdle(s->device);
		DestroyScene(s);
		if (s->pipeline) vkDestroyPipeline(s->device, s->pipeline, nullptr);
		if (s->playout) vkDestroyPipelineLayout(s->device, s->playout, nullptr);
		if (s->dpool) vkDestroyDescriptorPool(s->device, s->dpool, nullptr);
		if (s->dsl) vkDestroyDescriptorSetLayout(s->device, s->dsl, nullptr);
		if (s->staging) vkDestroyBuffer(s->device, s->staging, nullptr);
		if (s->staging_memory) vkFreeMemory(s->device, s->staging_memory, nullptr);
		if (s->ov_sampler) vkDestroySampler(s->device, s->ov_sampler, nullptr);
		if (s->ov_view) vkDestroyImageView(s->device, s->ov_view, nullptr);
		if (s->ov_image) vkDestroyImage(s->device, s->ov_image, nullptr);
		if (s->ov_memory) vkFreeMemory(s->device, s->ov_memory, nullptr);
		DestroySwapchain(s);
		if (s->sem_acquire) vkDestroySemaphore(s->device, s->sem_acquire, nullptr);
		if (s->fence) vkDestroyFence(s->device, s->fence, nullptr);
		if (s->cmdpool) vkDestroyCommandPool(s->device, s->cmdpool, nullptr);
		vkDestroyDevice(s->device, nullptr);
	}
	if (s->surface) vkDestroySurfaceKHR(s->instance, s->surface, nullptr);
	if (s->messenger)
	{
		auto destroy = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(s->instance, "vkDestroyDebugUtilsMessengerEXT");
		if (destroy)
			destroy(s->instance, s->messenger, nullptr);
	}
	if (s->instance) vkDestroyInstance(s->instance, nullptr);
	delete s;
}

} // namespace

extern "C" pt_backend_t *pt_rtx_create(const pt_create_t *ci, char *err, int errlen)
{
	RtxBackend *s = new RtxBackend;
	s->base.name = "RTX path tracer";
	s->base.destroy = Destroy;
	s->base.load_world = LoadWorld;
	s->base.texture_create = TextureCreate;
	s->base.texture_destroy = TextureDestroy;
	s->base.texture_update = TextureUpdate;
	s->base.render_view = RenderView;
	s->base.present = Present;
	s->base.stats = Stats;
	s->base.read_pixels = ReadPixels;
	s->textures.resize(kMaxTextures);
	s->log = ci->log;
	s->hwnd = (HWND)ci->hwnd;
	s->width = ci->width;
	s->height = ci->height;

	try
	{
		VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
		app.pApplicationName = "q2pt";
		app.apiVersion = VK_API_VERSION_1_3;

		// PT_VK_VALIDATE in the environment turns on Vulkan's own checking,
		// if the SDK's layer is installed; what it finds goes to the log
		const bool validate = getenv("PT_VK_VALIDATE") != nullptr && HasValidationLayer();
		const char *const iexts[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
			VK_EXT_DEBUG_UTILS_EXTENSION_NAME};
		const char *const layers[] = {"VK_LAYER_KHRONOS_validation"};
		VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
		ici.pApplicationInfo = &app;
		ici.enabledExtensionCount = validate ? 3 : 2;
		ici.ppEnabledExtensionNames = iexts;
		ici.enabledLayerCount = validate ? 1 : 0;
		ici.ppEnabledLayerNames = layers;
		Check(vkCreateInstance(&ici, nullptr, &s->instance), "vkCreateInstance");
		if (validate)
			CreateMessenger(s);

		VkWin32SurfaceCreateInfoKHR wci{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
		wci.hinstance = (HINSTANCE)ci->hinstance;
		wci.hwnd = s->hwnd;
		Check(vkCreateWin32SurfaceKHR(s->instance, &wci, nullptr, &s->surface), "vkCreateWin32SurfaceKHR");

		PickDevice(s);
		CreateDevice(s);
		CreateSwapchain(s);
		if (!s->swapchain)
			Throw("window has no drawable area");
		CreateOverlay(s);
		CreateScene(s);
		CreatePipeline(s, s->surface_format.format);
	}
	catch (const Fail &f)
	{
		snprintf(err, errlen, "%s", f.msg.c_str());
		Destroy(&s->base);
		return nullptr;
	}
	return &s->base;
}
