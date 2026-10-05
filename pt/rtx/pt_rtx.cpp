// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jonathan Ferguson
//
// Vulkan ray tracing backend. For now it brings up a ray tracing capable
// device and a swapchain and presents the overlay; the tracer comes later.

#include "../include/pt.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define VK_USE_PLATFORM_WIN32_KHR
#include <windows.h>
#include <vulkan/vulkan.h>

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

const uint32_t VENDOR_NVIDIA = 0x10DE;

const char *const kDeviceExtensions[] = {
	VK_KHR_SWAPCHAIN_EXTENSION_NAME,
	VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
	VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
	VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
};
const uint32_t kNumDeviceExtensions = sizeof(kDeviceExtensions) / sizeof(kDeviceExtensions[0]);

struct Push
{
	float	view[4];
	float	screen[2];
	float	has_view;
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
	VkFence				fence = VK_NULL_HANDLE;
	VkSemaphore			sem_acquire = VK_NULL_HANDLE;
	VkSemaphore			sem_render = VK_NULL_HANDLE;

	VkSwapchainKHR		swapchain = VK_NULL_HANDLE;
	VkSurfaceFormatKHR	surface_format{};
	VkExtent2D			extent{};
	std::vector<VkImage>		swap_images;
	std::vector<VkImageView>	swap_views;

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
	VkPhysicalDeviceRayTracingPipelineFeaturesKHR		rt{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR};

	RtFeatures()
	{
		f2.pNext = &v12;
		v12.pNext = &v13;
		v13.pNext = &as;
		as.pNext = &rt;
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
			if (!feat.as.accelerationStructure || !feat.rt.rayTracingPipeline ||
				!feat.v12.bufferDeviceAddress || !feat.v13.dynamicRendering || !feat.v13.synchronization2)
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
	feat.rt.rayTracingPipeline = VK_TRUE;

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

	VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
	fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
	Check(vkCreateFence(s->device, &fci, nullptr, &s->fence), "vkCreateFence");

	VkSemaphoreCreateInfo sci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
	Check(vkCreateSemaphore(s->device, &sci, nullptr, &s->sem_acquire), "vkCreateSemaphore");
	Check(vkCreateSemaphore(s->device, &sci, nullptr, &s->sem_render), "vkCreateSemaphore");
}

// ------------------------------------------------------------- swapchain

void DestroySwapchain(RtxBackend *s)
{
	for (VkImageView v : s->swap_views)
		vkDestroyImageView(s->device, v, nullptr);
	s->swap_views.clear();
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
	VkDescriptorSetLayoutBinding binding{};
	binding.binding = 0;
	binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	binding.descriptorCount = 1;
	binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	VkDescriptorSetLayoutCreateInfo dlci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
	dlci.bindingCount = 1;
	dlci.pBindings = &binding;
	Check(vkCreateDescriptorSetLayout(s->device, &dlci, nullptr, &s->dsl), "vkCreateDescriptorSetLayout");

	VkDescriptorPoolSize psize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
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

void RenderView(pt_backend_t *b, const pt_view_t *view)
{
	RtxBackend *s = Self(b);
	s->view = *view;
	s->has_view = true;
}

// no tracer yet, so there is nothing to do with a world
void LoadWorld(pt_backend_t *, const pt_world_t *)
{
}

const char *Stats(pt_backend_t *)
{
	return "";
}

int TextureCreate(pt_backend_t *, const pt_texture_t *)
{
	return -1;
}

void TextureDestroy(pt_backend_t *, int)
{
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
	push.has_view = s->has_view ? 1.0f : 0.0f;

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
	signal.semaphore = s->sem_render;
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
	pi.pWaitSemaphores = &s->sem_render;
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
}

void Destroy(pt_backend_t *b)
{
	RtxBackend *s = Self(b);
	if (s->device)
	{
		vkDeviceWaitIdle(s->device);
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
		if (s->sem_render) vkDestroySemaphore(s->device, s->sem_render, nullptr);
		if (s->sem_acquire) vkDestroySemaphore(s->device, s->sem_acquire, nullptr);
		if (s->fence) vkDestroyFence(s->device, s->fence, nullptr);
		if (s->cmdpool) vkDestroyCommandPool(s->device, s->cmdpool, nullptr);
		vkDestroyDevice(s->device, nullptr);
	}
	if (s->surface) vkDestroySurfaceKHR(s->instance, s->surface, nullptr);
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
	s->base.render_view = RenderView;
	s->base.present = Present;
	s->base.stats = Stats;
	s->log = ci->log;
	s->hwnd = (HWND)ci->hwnd;
	s->width = ci->width;
	s->height = ci->height;

	try
	{
		VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
		app.pApplicationName = "q2pt";
		app.apiVersion = VK_API_VERSION_1_3;

		const char *const iexts[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME};
		VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
		ici.pApplicationInfo = &app;
		ici.enabledExtensionCount = 2;
		ici.ppEnabledExtensionNames = iexts;
		Check(vkCreateInstance(&ici, nullptr, &s->instance), "vkCreateInstance");

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
