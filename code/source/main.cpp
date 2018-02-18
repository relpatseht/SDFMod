#include <vulkan/vulkan.h>
#include <shaderc/shaderc.hpp>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <iostream>
#include <sstream>
#include <cassert>
#include <unordered_set>
#include <algorithm>
#include <array>
#include <tuple>
#include <chrono>

#define STACK_ARRAY(TYPE, COUNT) (TYPE*)alloca(sizeof(TYPE) * (COUNT))
#define ARRAY_COUNT(ARR) (sizeof((ARR)) / sizeof((ARR)[0]))

typedef std::unordered_set<std::string> StringSet;


namespace vk
{
	static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT objType, uint64_t obj, size_t location, int32_t code, const char* layerPrefix, const char* msg, void* userData)
	{
		std::ostringstream err;

		err << "[Vk][";
		err << ((flags & VK_DEBUG_REPORT_ERROR_BIT_EXT) ? "E" : " ");
		err << ((flags & VK_DEBUG_REPORT_WARNING_BIT_EXT) ? "W" : " ");
		err << ((flags & VK_DEBUG_REPORT_INFORMATION_BIT_EXT) ? "I" : " ");
		err << ((flags & VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT) ? "P" : " ");
		err << ((flags & VK_DEBUG_REPORT_DEBUG_BIT_EXT) ? "D" : " ");
		err << "] " << msg;;

		std::cout << err.str() << std::endl;

		return VK_FALSE;
	}

	namespace init
	{
		VkInstance CreateInstance(const char * const *layers, uint32_t layerCount)
		{
			VkApplicationInfo appInfo = {};
			appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
			appInfo.pApplicationName = "SDFMod";
			appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
			appInfo.pEngineName = "RelEng";
			appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
			appInfo.apiVersion = VK_API_VERSION_1_0;

			uint32_t glfwExtensionCount = 0;
			const char** glfwExtensions;

			glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

			std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);
			extensions.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);

			VkInstanceCreateInfo createInfo = {};
			createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
			createInfo.pApplicationInfo = &appInfo;
			createInfo.enabledExtensionCount = (uint32_t)extensions.size();
			createInfo.ppEnabledExtensionNames = extensions.data();
			createInfo.enabledLayerCount = layerCount;
			createInfo.ppEnabledLayerNames = layers;

			VkInstance inst;
			vkCreateInstance(&createInfo, nullptr, &inst);

			return inst;
		}

		VkDebugReportCallbackEXT CreateDebugCallback(VkInstance inst)
		{
			VkDebugReportCallbackEXT callback;
			VkDebugReportCallbackCreateInfoEXT debugInfo = {};
			debugInfo.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
			debugInfo.flags = ~0u;
			debugInfo.pfnCallback = DebugCallback;

			auto func = (PFN_vkCreateDebugReportCallbackEXT)vkGetInstanceProcAddr(inst, "vkCreateDebugReportCallbackEXT");
			func(inst, &debugInfo, nullptr, &callback);

			return callback;
		}

		VkSurfaceKHR CreateSurface(VkInstance inst, GLFWwindow *window)
		{
			VkSurfaceKHR surface;
			glfwCreateWindowSurface(inst, window, nullptr, &surface);

			return surface;
		}
	}

	namespace queue
	{
		bool GetQueueFamilyIndices(VkSurfaceKHR surface, VkPhysicalDevice device, uint32_t *outoptGfxFamily, uint32_t *outoptPresFamily)
		{
			uint32_t queueFamilyCount = 0;
			vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

			VkQueueFamilyProperties * const queueFamilies = STACK_ARRAY(VkQueueFamilyProperties, queueFamilyCount);
			vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies);

			uint32_t presFamily = ~0u;
			uint32_t gfxFamily = ~0u;
			for (uint32_t queueFamilyIndex = 0; queueFamilyIndex < queueFamilyCount; ++queueFamilyIndex)
			{
				const VkQueueFamilyProperties &queueFamily = queueFamilies[queueFamilyIndex];

				if (queueFamily.queueCount > 0)
				{
					VkBool32 presentSupport = false;

					vkGetPhysicalDeviceSurfaceSupportKHR(device, queueFamilyIndex, surface, &presentSupport);
					if (presentSupport)
						presFamily = queueFamilyIndex;

					if ((queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0)
						gfxFamily = queueFamilyIndex;

					if (gfxFamily < queueFamilyCount && presFamily < queueFamilyCount)
					{
						if(outoptGfxFamily)
							*outoptGfxFamily = gfxFamily;

						if(outoptPresFamily)
							*outoptPresFamily = presFamily;

						return true;
					}
				}
			}

			return false;
		}
	}

	namespace memory
	{
		uint32_t FindMemoryType(VkPhysicalDevice phyDev, uint32_t typeFilter, VkMemoryPropertyFlags properties) 
		{
			VkPhysicalDeviceMemoryProperties memProperties;
			vkGetPhysicalDeviceMemoryProperties(phyDev, &memProperties);

			for (uint32_t memTypeIndex = 0; memTypeIndex < memProperties.memoryTypeCount; ++memTypeIndex)
				if ((typeFilter & (1 << memTypeIndex)) != 0)
					if ((memProperties.memoryTypes[memTypeIndex].propertyFlags & properties) == properties)
						return memTypeIndex;

			assert(0);
			return ~0u;
		}
	}

	namespace cmd
	{
		VkCommandBuffer BeginOneShotCommands(VkDevice dev, VkCommandPool commandPool) 
		{
			VkCommandBufferAllocateInfo allocInfo = {};
			allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
			allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			allocInfo.commandPool = commandPool;
			allocInfo.commandBufferCount = 1;

			VkCommandBuffer commandBuffer;
			vkAllocateCommandBuffers(dev, &allocInfo, &commandBuffer);

			VkCommandBufferBeginInfo beginInfo = {};
			beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
			beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

			vkBeginCommandBuffer(commandBuffer, &beginInfo);

			return commandBuffer;
		}

		void EndOneShotCommands(VkDevice dev, VkQueue gfxQueue, VkCommandPool commandPool, VkCommandBuffer commandBuffer) 
		{
			vkEndCommandBuffer(commandBuffer);

			VkSubmitInfo submitInfo = {};
			submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
			submitInfo.commandBufferCount = 1;
			submitInfo.pCommandBuffers = &commandBuffer;

			vkQueueSubmit(gfxQueue, 1, &submitInfo, VK_NULL_HANDLE);
			vkQueueWaitIdle(gfxQueue);

			vkFreeCommandBuffers(dev, commandPool, 1, &commandBuffer);
		}

		VkCommandPool CreateCommandPool(VkSurfaceKHR surf, VkPhysicalDevice phyDev, VkDevice dev) 
		{
			uint32_t gfxFamily;
			queue::GetQueueFamilyIndices(surf, phyDev, &gfxFamily, nullptr);

			VkCommandPoolCreateInfo poolInfo = {};
			poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
			poolInfo.queueFamilyIndex = gfxFamily;

			VkCommandPool commandPool;
			vkCreateCommandPool(dev, &poolInfo, nullptr, &commandPool);

			return commandPool;
		}
	}

	namespace image
	{
		struct Image
		{
			VkImage img;
			VkDeviceMemory mem;
		};

		Image CreateImage(VkPhysicalDevice phyDev, VkDevice dev, uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties)
		{
			VkImageCreateInfo imageInfo = {};
			imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
			imageInfo.imageType = VK_IMAGE_TYPE_2D;
			imageInfo.extent.width = width;
			imageInfo.extent.height = height;
			imageInfo.extent.depth = 1;
			imageInfo.mipLevels = 1;
			imageInfo.arrayLayers = 1;
			imageInfo.format = format;
			imageInfo.tiling = tiling;
			imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			imageInfo.usage = usage;
			imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
			imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

			Image img;
			vkCreateImage(dev, &imageInfo, nullptr, &img.img);

			VkMemoryRequirements memRequirements;
			vkGetImageMemoryRequirements(dev, img.img, &memRequirements);

			VkMemoryAllocateInfo allocInfo = {};
			allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			allocInfo.allocationSize = memRequirements.size;
			allocInfo.memoryTypeIndex = memory::FindMemoryType(phyDev, memRequirements.memoryTypeBits, properties);

			vkAllocateMemory(dev, &allocInfo, nullptr, &img.mem);
			vkBindImageMemory(dev, img.img, img.mem, 0);

			return img;
		}

		VkImageView CreateImageView(VkDevice dev, VkImage image, VkFormat format, VkImageAspectFlags aspectFlags) {
			VkImageViewCreateInfo viewInfo = {};
			viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			viewInfo.image = image;
			viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
			viewInfo.format = format;
			viewInfo.subresourceRange.aspectMask = aspectFlags;
			viewInfo.subresourceRange.baseMipLevel = 0;
			viewInfo.subresourceRange.levelCount = 1;
			viewInfo.subresourceRange.baseArrayLayer = 0;
			viewInfo.subresourceRange.layerCount = 1;

			VkImageView imageView;
			vkCreateImageView(dev, &viewInfo, nullptr, &imageView);

			return imageView;
		}

		void TransitionImageLayout(VkDevice dev, VkQueue gfxQueue, VkCommandPool cmdPool, VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout) {
			VkCommandBuffer commandBuffer = cmd::BeginOneShotCommands(dev, cmdPool);

			VkImageMemoryBarrier barrier = {};
			barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			barrier.oldLayout = oldLayout;
			barrier.newLayout = newLayout;
			barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.image = image;

			if (newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
				barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;

				if (format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT)
					barrier.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
			}
			else
				barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

			barrier.subresourceRange.baseMipLevel = 0;
			barrier.subresourceRange.levelCount = 1;
			barrier.subresourceRange.baseArrayLayer = 0;
			barrier.subresourceRange.layerCount = 1;

			VkPipelineStageFlags sourceStage;
			VkPipelineStageFlags destinationStage;

			if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
			{
				barrier.srcAccessMask = 0;
				barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

				sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
				destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
			}
			else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
			{
				barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
				barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

				sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
				destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
			}
			else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
			{
				barrier.srcAccessMask = 0;
				barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

				sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
				destinationStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
			}
			else
				assert(0);

			vkCmdPipelineBarrier(
				commandBuffer,
				sourceStage, destinationStage,
				0,
				0, nullptr,
				0, nullptr,
				1, &barrier
			);

			cmd::EndOneShotCommands(dev, gfxQueue, cmdPool, commandBuffer);
		}
	}

	namespace device
	{
		VkSurfaceFormatKHR GetPhysicalSurfaceFormat(VkSurfaceKHR surface, VkPhysicalDevice phyDev)
		{
			uint32_t formatCount;
			vkGetPhysicalDeviceSurfaceFormatsKHR(phyDev, surface, &formatCount, nullptr);

			if (formatCount != 0)
			{
				VkSurfaceFormatKHR * const formats = STACK_ARRAY(VkSurfaceFormatKHR, formatCount);
				vkGetPhysicalDeviceSurfaceFormatsKHR(phyDev, surface, &formatCount, formats);

				if (formatCount == 1 && formats[0].format == VK_FORMAT_UNDEFINED)
					return{ VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };

				for (uint32_t formatIndex = 0; formatIndex < formatCount; ++formatIndex)
					if (formats[formatIndex].format == VK_FORMAT_B8G8R8A8_UNORM && formats[formatIndex].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
						return formats[formatIndex];

				return formats[0];
			}

			return { VK_FORMAT_UNDEFINED, VK_COLOR_SPACE_MAX_ENUM_KHR };
		}

		VkPresentModeKHR GetPhysicalPresentMode(VkSurfaceKHR surface, VkPhysicalDevice phyDev)
		{
			uint32_t presentModeCount;
			vkGetPhysicalDeviceSurfacePresentModesKHR(phyDev, surface, &presentModeCount, nullptr);

			if (presentModeCount != 0)
			{
				VkPresentModeKHR * const presentModes = STACK_ARRAY(VkPresentModeKHR, presentModeCount);
				vkGetPhysicalDeviceSurfacePresentModesKHR(phyDev, surface, &presentModeCount, presentModes);

				VkPresentModeKHR bestMode = VK_PRESENT_MODE_FIFO_KHR; // guarenteed available
				for (uint32_t modeIndex = 0; modeIndex<presentModeCount; ++modeIndex)
					if (presentModes[modeIndex] == VK_PRESENT_MODE_MAILBOX_KHR)
						return presentModes[modeIndex];

				return bestMode;
			}

			return VK_PRESENT_MODE_MAX_ENUM_KHR;
		}

		bool DeviceSupportsExtension(VkPhysicalDevice device, const StringSet &requiredExtensions)
		{
			uint32_t extensionCount;
			vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

			VkExtensionProperties * const availableExtensions = STACK_ARRAY(VkExtensionProperties, extensionCount);
			vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions);

			unsigned foundExtensions = 0;
			for (uint32_t extIndex = 0; extIndex < extensionCount; ++extIndex)
				if (requiredExtensions.count(availableExtensions[extIndex].extensionName) > 0)
					++foundExtensions;

			return foundExtensions == requiredExtensions.size();
		}

		bool DeviceIsSuitable(VkSurfaceKHR surface, VkPhysicalDevice device, const StringSet &requiredExtensions)
		{
			VkPhysicalDeviceFeatures supportedFeatures;
			vkGetPhysicalDeviceFeatures(device, &supportedFeatures);

			if (supportedFeatures.samplerAnisotropy)
			{
				if (queue::GetQueueFamilyIndices(surface, device, nullptr, nullptr))
				{
					if (DeviceSupportsExtension(device, requiredExtensions))
					{
						if (GetPhysicalPresentMode(surface, device) != VK_PRESENT_MODE_MAX_ENUM_KHR)
							return GetPhysicalSurfaceFormat(surface, device).format != VK_FORMAT_UNDEFINED;
					}
				}
			}

			return false;
		}

		VkPhysicalDevice ChoosePhysicalDevice(VkInstance inst, VkSurfaceKHR surface, const StringSet &requiredExtensions)
		{
			uint32_t deviceCount = 0;
			vkEnumeratePhysicalDevices(inst, &deviceCount, nullptr);

			assert(deviceCount > 0);

			VkPhysicalDevice * const devices = STACK_ARRAY(VkPhysicalDevice, deviceCount);
			vkEnumeratePhysicalDevices(inst, &deviceCount, devices);

			for (uint32_t deviceIndex = 0; deviceIndex < deviceCount; ++deviceIndex)
			{
				if (DeviceIsSuitable(surface, devices[deviceIndex], requiredExtensions))
					return devices[deviceIndex];
			}

			assert(0);
			return nullptr;
		}

		VkDevice CreateLogicalDevice(VkSurfaceKHR surface, VkPhysicalDevice phyDev, uint32_t gfxQueueFamilyIndex, uint32_t presQueueFamilyIndex, const char * const *deviceExtensions, uint32_t deviceExtensionCount, const char * const *layers, uint32_t layerCount)
		{
			const float queuePriority = 1.0f;
			uint32_t queueCreateInfoCount;
			VkDeviceQueueCreateInfo queueCreateInfos[2] = {};

			queueCreateInfos[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
			queueCreateInfos[0].queueFamilyIndex = gfxQueueFamilyIndex;
			queueCreateInfos[0].queueCount = 1;
			queueCreateInfos[0].pQueuePriorities = &queuePriority;

			if (presQueueFamilyIndex != gfxQueueFamilyIndex)
			{
				queueCreateInfoCount = 2;

				queueCreateInfos[1].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
				queueCreateInfos[1].queueFamilyIndex = presQueueFamilyIndex;
				queueCreateInfos[1].queueCount = 1;
				queueCreateInfos[1].pQueuePriorities = &queuePriority;
			}
			else
				queueCreateInfoCount = 1;

			VkPhysicalDeviceFeatures deviceFeatures = {};
			deviceFeatures.samplerAnisotropy = VK_TRUE;

			VkDeviceCreateInfo createInfo = {};
			createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
			createInfo.queueCreateInfoCount = queueCreateInfoCount;
			createInfo.pQueueCreateInfos = queueCreateInfos;
			createInfo.pEnabledFeatures = &deviceFeatures;
			createInfo.enabledExtensionCount = deviceExtensionCount;
			createInfo.ppEnabledExtensionNames = deviceExtensions;
			createInfo.enabledLayerCount = layerCount;
			createInfo.ppEnabledLayerNames = layers;

			VkDevice device;
			vkCreateDevice(phyDev, &createInfo, nullptr, &device);

			return device;
		}
	}

	namespace shader
	{
		bool GLSLtoSPV(VkShaderStageFlagBits shaderType, const char *shaderTxt, std::vector<uint32_t> *outSpirv) 
		{
			shaderc::Compiler compiler;
			shaderc::CompileOptions options;

			options.SetWarningsAsErrors();
			options.SetOptimizationLevel(shaderc_optimization_level_size);

			shaderc_shader_kind kind;

			switch (shaderType)
			{
				case VK_SHADER_STAGE_VERTEX_BIT:
					kind = shaderc_glsl_default_vertex_shader;
				break;
				case VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT:
					kind = shaderc_glsl_default_tess_control_shader;
				break;
				case VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT:
					kind = shaderc_glsl_default_tess_evaluation_shader;
				break;
				case VK_SHADER_STAGE_GEOMETRY_BIT:
					kind = shaderc_glsl_default_geometry_shader;
				break;
				case VK_SHADER_STAGE_FRAGMENT_BIT:
					kind = shaderc_glsl_default_fragment_shader;
				break;
				case VK_SHADER_STAGE_COMPUTE_BIT:
					kind = shaderc_glsl_default_compute_shader;
				break;
				default:
					kind = shaderc_glsl_infer_from_source;
			}

			shaderc::SpvCompilationResult module = compiler.CompileGlslToSpv(shaderTxt, kind, "shader", options);

			if (module.GetCompilationStatus() != shaderc_compilation_status_success)
			{
				std::cout << "[SPV] " << module.GetErrorMessage() << std::endl;
				return false;
			}

			outSpirv->clear();
			outSpirv->insert(outSpirv->begin(), module.cbegin(), module.cend());
			return true;
		}

		VkShaderModule CreateShaderModule(VkDevice dev, const std::vector<uint32_t> &spirv)
		{
			VkShaderModuleCreateInfo createInfo = {};
			createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			createInfo.codeSize = spirv.size() * sizeof(uint32_t);
			createInfo.pCode = spirv.data();

			VkShaderModule shaderModule;
			vkCreateShaderModule(dev, &createInfo, nullptr, &shaderModule);

			return shaderModule;
		}
	}

	namespace vertex
	{
		struct UniformBufferObject 
		{
			glm::mat4 model;
			glm::mat4 view;
			glm::mat4 proj;
		};

		struct Vertex 
		{
			glm::vec3 pos;
			glm::vec3 color;
		};

		VkVertexInputBindingDescription GetBindingDescription() 
		{
			VkVertexInputBindingDescription bindingDescription = {};
			bindingDescription.binding = 0;
			bindingDescription.stride = sizeof(Vertex);
			bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

			return bindingDescription;
		}

		std::array<VkVertexInputAttributeDescription, 2> GetAttributeDescriptions() 
		{
			std::array<VkVertexInputAttributeDescription, 2> attributeDescriptions = {};

			attributeDescriptions[0].binding = 0;
			attributeDescriptions[0].location = 0;
			attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
			attributeDescriptions[0].offset = offsetof(Vertex, pos);

			attributeDescriptions[1].binding = 0;
			attributeDescriptions[1].location = 1;
			attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
			attributeDescriptions[1].offset = offsetof(Vertex, color);

			return attributeDescriptions;
		}
	}

	namespace render
	{
		VkFormat OptimalDepthFormat(VkPhysicalDevice phyDev)
		{
			VkFormat orderedDepthFormats[] = { VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT };

			for (VkFormat format : orderedDepthFormats)
			{
				VkFormatProperties props;
				vkGetPhysicalDeviceFormatProperties(phyDev, format, &props);

				if ((props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0)
					return format;
			}

			assert(0);
			return VK_FORMAT_UNDEFINED;
		}

		VkDescriptorSetLayout CreateDescriptorSetLayout(VkDevice dev)
		{
			VkDescriptorSetLayoutBinding uboLayoutBinding = {};
			uboLayoutBinding.binding = 0;
			uboLayoutBinding.descriptorCount = 1;
			uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			uboLayoutBinding.pImmutableSamplers = nullptr;
			uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

			VkDescriptorSetLayoutBinding bindings[] = { uboLayoutBinding };
			VkDescriptorSetLayoutCreateInfo layoutInfo = {};
			layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			layoutInfo.bindingCount = ARRAY_COUNT(bindings);
			layoutInfo.pBindings = bindings;

			VkDescriptorSetLayout descriptorSetLayout;
			vkCreateDescriptorSetLayout(dev, &layoutInfo, nullptr, &descriptorSetLayout);

			return descriptorSetLayout;
		}

		VkDescriptorPool CreateDescriptorPool(VkDevice dev) 
		{
			VkDescriptorPoolSize poolSizes[1] = {};
			poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			poolSizes[0].descriptorCount = 1;

			VkDescriptorPoolCreateInfo poolInfo = {};
			poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			poolInfo.poolSizeCount = ARRAY_COUNT(poolSizes);
			poolInfo.pPoolSizes = poolSizes;
			poolInfo.maxSets = 1;

			VkDescriptorPool descriptorPool;
			vkCreateDescriptorPool(dev, &poolInfo, nullptr, &descriptorPool);

			return descriptorPool;
		}

		VkDescriptorSet CreateDescriptorSet(VkDevice dev, VkDescriptorSetLayout descSetLayout, VkDescriptorPool descPool, VkBuffer uniformBuf)
		{
			VkDescriptorSetLayout layouts[] = { descSetLayout };
			VkDescriptorSetAllocateInfo allocInfo = {};
			allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			allocInfo.descriptorPool = descPool;
			allocInfo.descriptorSetCount = 1;
			allocInfo.pSetLayouts = layouts;

			VkDescriptorSet descSet;
			if (vkAllocateDescriptorSets(dev, &allocInfo, &descSet) != VK_SUCCESS) {
				throw std::runtime_error("failed to allocate descriptor set!");
			}

			VkDescriptorBufferInfo bufferInfo = {};
			bufferInfo.buffer = uniformBuf;
			bufferInfo.offset = 0;
			bufferInfo.range = sizeof(vertex::UniformBufferObject);
			
			VkWriteDescriptorSet descriptorWrites[1] = {};

			descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			descriptorWrites[0].dstSet = descSet;
			descriptorWrites[0].dstBinding = 0;
			descriptorWrites[0].dstArrayElement = 0;
			descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			descriptorWrites[0].descriptorCount = 1;
			descriptorWrites[0].pBufferInfo = &bufferInfo;
			
			vkUpdateDescriptorSets(dev, ARRAY_COUNT(descriptorWrites), descriptorWrites, 0, nullptr);

			return descSet;
		}

		VkRenderPass CreateRenderPass(VkSurfaceKHR surface, VkPhysicalDevice phyDev, VkDevice dev)
		{
			VkAttachmentDescription colorAttachment = {};
			colorAttachment.format = device::GetPhysicalSurfaceFormat(surface, phyDev).format;
			colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
			colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
			colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

			VkAttachmentDescription depthAttachment = {};
			depthAttachment.format = OptimalDepthFormat(phyDev);
			depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
			depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
			depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

			VkAttachmentReference colorAttachmentRef = {};
			colorAttachmentRef.attachment = 0;
			colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

			VkAttachmentReference depthAttachmentRef = {};
			depthAttachmentRef.attachment = 1;
			depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

			VkSubpassDescription subpass = {};
			subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
			subpass.colorAttachmentCount = 1;
			subpass.pColorAttachments = &colorAttachmentRef;
			subpass.pDepthStencilAttachment = &depthAttachmentRef;

			VkSubpassDependency dependency = {};
			dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
			dependency.dstSubpass = 0;
			dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dependency.srcAccessMask = 0;
			dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

			VkAttachmentDescription attachments[2] = { colorAttachment, depthAttachment };
			VkRenderPassCreateInfo renderPassInfo = {};
			renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
			renderPassInfo.attachmentCount = ARRAY_COUNT(attachments);
			renderPassInfo.pAttachments = attachments;
			renderPassInfo.subpassCount = 1;
			renderPassInfo.pSubpasses = &subpass;
			renderPassInfo.dependencyCount = 1;
			renderPassInfo.pDependencies = &dependency;

			VkRenderPass renderPass;
			vkCreateRenderPass(dev, &renderPassInfo, nullptr, &renderPass);

			return renderPass;
		}

		std::tuple<VkPipeline, VkPipelineLayout> CreateGraphicsPipeline(VkDevice dev, VkRenderPass renderPass, VkDescriptorSetLayout descSetLayout, VkExtent2D viewportExtents)
		{
			std::vector<uint32_t> vertSpirv, fragSpirv;

			shader::GLSLtoSPV(VK_SHADER_STAGE_VERTEX_BIT, R"##(
			#version 450
			#extension GL_ARB_separate_shader_objects : enable

			layout(binding = 0) uniform UniformBufferObject {
				mat4 model;
				mat4 view;
				mat4 proj;
			} ubo;

			layout(location = 0) in vec3 inPosition;
			layout(location = 1) in vec3 inColor;

			layout(location = 0) out vec3 fragColor;

			out gl_PerVertex{
				vec4 gl_Position;
			};

			void main() {
				gl_Position = ubo.proj * ubo.view * ubo.model * vec4(inPosition, 1.0);
				fragColor = inColor;
			}
			)##", &vertSpirv);

			
			shader::GLSLtoSPV(VK_SHADER_STAGE_FRAGMENT_BIT, R"##(
			#version 450
			#extension GL_ARB_separate_shader_objects : enable

			layout(location = 0) in vec3 fragColor;
			layout(location = 0) out vec4 outColor;

			void main() {
				outColor = vec4(fragColor, 1.0);
			}
			)##", &fragSpirv);

			VkShaderModule vertShaderModule = shader::CreateShaderModule(dev, vertSpirv);
			VkShaderModule fragShaderModule = shader::CreateShaderModule(dev, fragSpirv);

			VkPipelineShaderStageCreateInfo vertShaderStageInfo = {};
			vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
			vertShaderStageInfo.module = vertShaderModule;
			vertShaderStageInfo.pName = "main";

			VkPipelineShaderStageCreateInfo fragShaderStageInfo = {};
			fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
			fragShaderStageInfo.module = fragShaderModule;
			fragShaderStageInfo.pName = "main";

			VkPipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, fragShaderStageInfo };

			VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
			vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

			auto bindingDescription = vertex::GetBindingDescription();
			auto attributeDescriptions = vertex::GetAttributeDescriptions();

			vertexInputInfo.vertexBindingDescriptionCount = 1;
			vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
			vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
			vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

			VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
			inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
			inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
			inputAssembly.primitiveRestartEnable = VK_FALSE;

			VkViewport viewport = {};
			viewport.x = 0.0f;
			viewport.y = 0.0f;
			viewport.width = (float)viewportExtents.width;
			viewport.height = (float)viewportExtents.height;
			viewport.minDepth = 0.0f;
			viewport.maxDepth = 1.0f;

			VkRect2D scissor = {};
			scissor.offset = { 0, 0 };
			scissor.extent = viewportExtents;

			VkPipelineViewportStateCreateInfo viewportState = {};
			viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
			viewportState.viewportCount = 1;
			viewportState.pViewports = &viewport;
			viewportState.scissorCount = 1;
			viewportState.pScissors = &scissor;

			VkPipelineRasterizationStateCreateInfo rasterizer = {};
			rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
			rasterizer.depthClampEnable = VK_FALSE;
			rasterizer.rasterizerDiscardEnable = VK_FALSE;
			rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
			rasterizer.lineWidth = 1.0f;
			rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
			rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
			rasterizer.depthBiasEnable = VK_FALSE;

			VkPipelineMultisampleStateCreateInfo multisampling = {};
			multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
			multisampling.sampleShadingEnable = VK_FALSE;
			multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

			VkPipelineDepthStencilStateCreateInfo depthStencil = {};
			depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
			depthStencil.depthTestEnable = VK_TRUE;
			depthStencil.depthWriteEnable = VK_TRUE;
			depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
			depthStencil.depthBoundsTestEnable = VK_FALSE;
			depthStencil.stencilTestEnable = VK_FALSE;

			VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
			colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
			colorBlendAttachment.blendEnable = VK_FALSE;

			VkPipelineColorBlendStateCreateInfo colorBlending = {};
			colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
			colorBlending.logicOpEnable = VK_FALSE;
			colorBlending.logicOp = VK_LOGIC_OP_COPY;
			colorBlending.attachmentCount = 1;
			colorBlending.pAttachments = &colorBlendAttachment;
			colorBlending.blendConstants[0] = 0.0f;
			colorBlending.blendConstants[1] = 0.0f;
			colorBlending.blendConstants[2] = 0.0f;
			colorBlending.blendConstants[3] = 0.0f;

			VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
			pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			pipelineLayoutInfo.setLayoutCount = 1;
			pipelineLayoutInfo.pSetLayouts = &descSetLayout;

			VkPipelineLayout pipelineLayout;
			vkCreatePipelineLayout(dev, &pipelineLayoutInfo, nullptr, &pipelineLayout);

			VkGraphicsPipelineCreateInfo pipelineInfo = {};
			pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
			pipelineInfo.stageCount = 2;
			pipelineInfo.pStages = shaderStages;
			pipelineInfo.pVertexInputState = &vertexInputInfo;
			pipelineInfo.pInputAssemblyState = &inputAssembly;
			pipelineInfo.pViewportState = &viewportState;
			pipelineInfo.pRasterizationState = &rasterizer;
			pipelineInfo.pMultisampleState = &multisampling;
			pipelineInfo.pDepthStencilState = &depthStencil;
			pipelineInfo.pColorBlendState = &colorBlending;
			pipelineInfo.layout = pipelineLayout;
			pipelineInfo.renderPass = renderPass;
			pipelineInfo.subpass = 0;
			pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

			VkPipeline gfxPipeline;
			vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &gfxPipeline);

			vkDestroyShaderModule(dev, fragShaderModule, nullptr);
			vkDestroyShaderModule(dev, vertShaderModule, nullptr);

			return std::make_tuple(gfxPipeline, pipelineLayout);
		}
	}

	namespace swap
	{
		struct SwapChain
		{
			VkSwapchainKHR swapChain; 
			VkFormat imageFormat;
			VkExtent2D extent;
			image::Image depth;
			VkImageView depthView;
			std::vector<VkImage> images;
			std::vector<VkImageView> imageViews;
			std::vector<VkFramebuffer> framebuffers;
			std::vector<VkCommandBuffer> commandBuffers;
		};

		VkExtent2D ChooseSwapExtent(GLFWwindow *window, const VkSurfaceCapabilitiesKHR& capabilities) 
		{
			if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
				return capabilities.currentExtent;
			else
			{
				int width, height;
				glfwGetWindowSize(window, &width, &height);

				VkExtent2D actualExtent{
					static_cast<uint32_t>(width),
					static_cast<uint32_t>(height)
				};

				actualExtent.width = std::max(capabilities.minImageExtent.width, std::min(capabilities.maxImageExtent.width, actualExtent.width));
				actualExtent.height = std::max(capabilities.minImageExtent.height, std::min(capabilities.maxImageExtent.height, actualExtent.height));

				return actualExtent;
			}
		}

		void CreateSwapChain(GLFWwindow *window, VkSurfaceKHR surface, VkPhysicalDevice phyDev, VkDevice dev, VkQueue gfxQueue, VkCommandPool cmdPool, VkRenderPass renderPass, SwapChain *outSwap)
		{
			uint32_t queueFamilies[2];
			queue::GetQueueFamilyIndices(surface, phyDev, &queueFamilies[0], &queueFamilies[1]);

			VkSurfaceCapabilitiesKHR capabilities;
			vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phyDev, surface, &capabilities);

			VkSurfaceFormatKHR surfaceFormat = device::GetPhysicalSurfaceFormat(surface, phyDev);
			VkPresentModeKHR presentMode = device::GetPhysicalPresentMode(surface, phyDev);
			VkExtent2D extent = ChooseSwapExtent(window, capabilities);

			uint32_t imageCount = capabilities.minImageCount + 1;
			if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount) 
				imageCount = capabilities.maxImageCount;
			
			VkSwapchainCreateInfoKHR createInfo = {};
			createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
			createInfo.surface = surface;
			createInfo.minImageCount = imageCount;
			createInfo.imageFormat = surfaceFormat.format;
			createInfo.imageColorSpace = surfaceFormat.colorSpace;
			createInfo.imageExtent = extent;
			createInfo.imageArrayLayers = 1;
			createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

			createInfo.pQueueFamilyIndices = queueFamilies;
			if (queueFamilies[0] == queueFamilies[1])
			{
				createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
				createInfo.queueFamilyIndexCount = 1;
			}
			else
			{
				createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
				createInfo.queueFamilyIndexCount = 2;
			}

			createInfo.preTransform = capabilities.currentTransform;
			createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
			createInfo.presentMode = presentMode;
			createInfo.clipped = VK_TRUE;

			VkSwapchainKHR swapChain;
			vkCreateSwapchainKHR(dev, &createInfo, nullptr, &swapChain);

			VkFormat depthFormat = render::OptimalDepthFormat(phyDev);
			image::Image depthImage = image::CreateImage(phyDev, dev, extent.width, extent.height, depthFormat, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
			VkImageView depthView = image::CreateImageView(dev, depthImage.img, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT);
			image::TransitionImageLayout(dev, gfxQueue, cmdPool, depthImage.img, depthFormat, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

			outSwap->images.resize(imageCount);
			outSwap->imageViews.resize(imageCount);
			outSwap->framebuffers.resize(imageCount);
			outSwap->commandBuffers.resize(imageCount);
			vkGetSwapchainImagesKHR(dev, swapChain, &imageCount, nullptr);
			vkGetSwapchainImagesKHR(dev, swapChain, &imageCount, outSwap->images.data());

			VkCommandBufferAllocateInfo allocInfo = {};
			allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
			allocInfo.commandPool = cmdPool;
			allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			allocInfo.commandBufferCount = (uint32_t)outSwap->commandBuffers.size();

			vkAllocateCommandBuffers(dev, &allocInfo, outSwap->commandBuffers.data());

			for (uint32_t imageIndex = 0; imageIndex < imageCount; ++imageIndex)
			{
				outSwap->imageViews[imageIndex] = image::CreateImageView(dev, outSwap->images[imageIndex], surfaceFormat.format, VK_IMAGE_ASPECT_COLOR_BIT);
				VkImageView attachments[2] = { outSwap->imageViews[imageIndex], depthView };

				VkFramebufferCreateInfo framebufferInfo = {};
				framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
				framebufferInfo.renderPass = renderPass;
				framebufferInfo.attachmentCount = ARRAY_COUNT(attachments);
				framebufferInfo.pAttachments = attachments;
				framebufferInfo.width = extent.width;
				framebufferInfo.height = extent.height;
				framebufferInfo.layers = 1;

				vkCreateFramebuffer(dev, &framebufferInfo, nullptr, &outSwap->framebuffers[imageIndex]);
			}

			outSwap->swapChain = swapChain;
			outSwap->imageFormat = surfaceFormat.format;
			outSwap->extent = extent; 
			outSwap->depth = depthImage;
			outSwap->depthView = depthView;
		}
	}

	namespace buffer
	{
		struct Buffer
		{
			VkBuffer buf;
			VkDeviceMemory mem;
		};

		Buffer CreateBuffer(VkPhysicalDevice phyDev, VkDevice dev, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties) 
		{
			VkBufferCreateInfo bufferInfo = {};
			bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			bufferInfo.size = size;
			bufferInfo.usage = usage;
			bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

			Buffer buf;
			vkCreateBuffer(dev, &bufferInfo, nullptr, &buf.buf);

			VkMemoryRequirements memRequirements;
			vkGetBufferMemoryRequirements(dev, buf.buf, &memRequirements);

			VkMemoryAllocateInfo allocInfo = {};
			allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			allocInfo.allocationSize = memRequirements.size;
			allocInfo.memoryTypeIndex = memory::FindMemoryType(phyDev, memRequirements.memoryTypeBits, properties);

			vkAllocateMemory(dev, &allocInfo, nullptr, &buf.mem);

			vkBindBufferMemory(dev, buf.buf, buf.mem, 0);

			return buf;
		}
		
		void CopyBuffer(VkDevice dev, VkCommandPool cmdPool, VkQueue gfxQueue, VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size) 
		{
			VkCommandBuffer commandBuffer = cmd::BeginOneShotCommands(dev, cmdPool);

			VkBufferCopy copyRegion = {};
			copyRegion.size = size;
			vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

			cmd::EndOneShotCommands(dev, gfxQueue, cmdPool, commandBuffer);
		}

		Buffer CreateVertexBuffer(VkPhysicalDevice phyDev, VkDevice dev, VkCommandPool cmdPool, VkQueue gfxQueue, const std::vector<vertex::Vertex> &vertices) 
		{
			VkDeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();

			Buffer staging = CreateBuffer(phyDev, dev, bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

			void* data;
			vkMapMemory(dev, staging.mem, 0, bufferSize, 0, &data);
			memcpy(data, vertices.data(), (size_t)bufferSize);
			vkUnmapMemory(dev, staging.mem);

			Buffer vertBuf = CreateBuffer(phyDev, dev, bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

			CopyBuffer(dev, cmdPool, gfxQueue, staging.buf, vertBuf.buf, bufferSize);

			vkDestroyBuffer(dev, staging.buf, nullptr);
			vkFreeMemory(dev, staging.mem, nullptr);

			return vertBuf;
		}

		Buffer CreateIndexBuffer(VkPhysicalDevice phyDev, VkDevice dev, VkCommandPool cmdPool, VkQueue gfxQueue, const std::vector<uint16_t> &indices)
		{
			VkDeviceSize bufferSize = sizeof(indices[0]) * indices.size();

			Buffer staging = CreateBuffer(phyDev, dev, bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

			void* data;
			vkMapMemory(dev, staging.mem, 0, bufferSize, 0, &data);
			memcpy(data, indices.data(), (size_t)bufferSize);
			vkUnmapMemory(dev, staging.mem);

			Buffer idxBuf = CreateBuffer(phyDev, dev, bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

			CopyBuffer(dev, cmdPool, gfxQueue, staging.buf, idxBuf.buf, bufferSize);

			vkDestroyBuffer(dev, staging.buf, nullptr);
			vkFreeMemory(dev, staging.mem, nullptr);

			return idxBuf;
		}

		Buffer CreateUniformBuffer(VkPhysicalDevice phyDev, VkDevice dev)
		{
			VkDeviceSize bufferSize = sizeof(vertex::UniformBufferObject);
			return CreateBuffer(phyDev, dev, bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		}
	}

	void FillCommandBuffers(swap::SwapChain *inoutSwap, VkRenderPass renderPass, VkPipeline gfxPipe, VkPipelineLayout gfxPipeLayout, VkDescriptorSet descSet, VkBuffer vertBuf, VkBuffer idxBuf, const std::vector<vertex::Vertex> &vertices, const std::vector<uint16_t> &indices)
	{
		for (size_t cmdBufIndex = 0; cmdBufIndex < inoutSwap->commandBuffers.size(); ++cmdBufIndex) 
		{
			VkCommandBuffer cmdBuf = inoutSwap->commandBuffers[cmdBufIndex];

			VkCommandBufferBeginInfo beginInfo = {};
			beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
			beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;

			vkBeginCommandBuffer(cmdBuf, &beginInfo);

			VkRenderPassBeginInfo renderPassInfo = {};
			renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
			renderPassInfo.renderPass = renderPass;
			renderPassInfo.framebuffer = inoutSwap->framebuffers[cmdBufIndex];
			renderPassInfo.renderArea.offset = { 0, 0 };
			renderPassInfo.renderArea.extent = inoutSwap->extent;

			std::array<VkClearValue, 2> clearValues = {};
			clearValues[0].color = { 0.0f, 0.0f, 0.0f, 1.0f };
			clearValues[1].depthStencil = { 1.0f, 0 };

			renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
			renderPassInfo.pClearValues = clearValues.data();

			vkCmdBeginRenderPass(cmdBuf, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

			vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, gfxPipe);

			VkBuffer vertexBuffers[] = { vertBuf };
			VkDeviceSize offsets[] = { 0 };
			vkCmdBindVertexBuffers(cmdBuf, 0, 1, vertexBuffers, offsets);

			vkCmdBindIndexBuffer(cmdBuf, idxBuf, 0, VK_INDEX_TYPE_UINT16);

			vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, gfxPipeLayout, 0, 1, &descSet, 0, nullptr);

			vkCmdDrawIndexed(cmdBuf, static_cast<uint32_t>(indices.size()), 1, 0, 0, 0);

			vkCmdEndRenderPass(cmdBuf);

			vkEndCommandBuffer(cmdBuf);
		}
	}

	struct VulkanWindow
	{
		VkInstance instance;
		VkDebugReportCallbackEXT callback;
		VkSurfaceKHR surface;

		VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
		VkDevice device;

		VkQueue graphicsQueue;
		VkQueue presentQueue;

		swap::SwapChain swapChain;

		VkRenderPass renderPass;
		VkDescriptorSetLayout descriptorSetLayout;
		VkPipelineLayout pipelineLayout;
		VkPipeline graphicsPipeline;

		VkCommandPool commandPool;
		
		VkImage textureImage;
		VkDeviceMemory textureImageMemory;
		VkImageView textureImageView;
		VkSampler textureSampler;

		buffer::Buffer vertBuf;
		buffer::Buffer indexBuf;
		buffer::Buffer uniformBuf;

		VkDescriptorPool descriptorPool;
		VkDescriptorSet descriptorSet;

		VkSemaphore imageAvailableSemaphore;
		VkSemaphore renderFinishedSemaphore;
	};

	const std::vector<vertex::Vertex> vertices = {
		{ { -0.5f, -0.5f,  0.0f },{ 1.0f, 0.0f, 0.0f } },
	    { {  0.5f, -0.5f,  0.0f },{ 0.0f, 1.0f, 0.0f } },
	    { {  0.5f,  0.5f,  0.0f },{ 0.0f, 0.0f, 1.0f } },
	    { { -0.5f,  0.5f,  0.0f },{ 1.0f, 1.0f, 1.0f } },
	    
	    { { -0.5f, -0.5f, -0.5f },{ 1.0f, 0.0f, 0.0f } },
	    { {  0.5f, -0.5f, -0.5f },{ 0.0f, 1.0f, 0.0f } },
	    { {  0.5f,  0.5f, -0.5f },{ 0.0f, 0.0f, 1.0f } },
	    { { -0.5f,  0.5f, -0.5f },{ 1.0f, 1.0f, 1.0f } }
	};

	const std::vector<uint16_t> indices = {
		0, 1, 2, 2, 3, 0,
		4, 5, 6, 6, 7, 4
	};

	bool InitVulkan(GLFWwindow *window, VulkanWindow *outV)
	{
		static const char *requiredExtensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
		static const StringSet requiredExtensionSet(&requiredExtensions[0], &requiredExtensions[0] + ARRAY_COUNT(requiredExtensions));
		static const char *layers[] = { "VK_LAYER_LUNARG_standard_validation" };

		VkInstance inst = init::CreateInstance(layers, ARRAY_COUNT(layers));
		outV->callback = init::CreateDebugCallback(inst);
		VkSurfaceKHR surf = init::CreateSurface(inst, window);
		VkPhysicalDevice phyDev = device::ChoosePhysicalDevice(inst, surf, requiredExtensionSet);

		uint32_t gfxQueueFamilyIndex, presQueueFamilyIndex;
		queue::GetQueueFamilyIndices(surf, phyDev, &gfxQueueFamilyIndex, &presQueueFamilyIndex);
		
		VkDevice dev = device::CreateLogicalDevice(surf, phyDev, gfxQueueFamilyIndex, presQueueFamilyIndex, requiredExtensions, ARRAY_COUNT(requiredExtensions), layers, ARRAY_COUNT(layers));
		
		VkQueue gfxQueue;
		vkGetDeviceQueue(dev, gfxQueueFamilyIndex, 0, &gfxQueue);
		vkGetDeviceQueue(dev, presQueueFamilyIndex, 0, &outV->presentQueue);

		VkRenderPass renderPass = render::CreateRenderPass(surf, phyDev, dev);
		VkCommandPool cmdPool = cmd::CreateCommandPool(surf, phyDev, dev);
		swap::CreateSwapChain(window, surf, phyDev, dev, gfxQueue, cmdPool, renderPass, &outV->swapChain);

		outV->renderPass = render::CreateRenderPass(surf, phyDev, dev);
		outV->descriptorSetLayout = render::CreateDescriptorSetLayout(dev);

		std::tie(outV->graphicsPipeline, outV->pipelineLayout) = render::CreateGraphicsPipeline(dev, outV->renderPass, outV->descriptorSetLayout, outV->swapChain.extent);

		outV->vertBuf = buffer::CreateVertexBuffer(phyDev, dev, cmdPool, gfxQueue, vertices);
		outV->indexBuf = buffer::CreateIndexBuffer(phyDev, dev, cmdPool, gfxQueue, indices);
		outV->uniformBuf = buffer::CreateUniformBuffer(phyDev, dev);

		outV->descriptorPool = render::CreateDescriptorPool(dev);
		outV->descriptorSet = render::CreateDescriptorSet(dev, outV->descriptorSetLayout, outV->descriptorPool, outV->uniformBuf.buf);

		outV->instance = inst;
		outV->surface = surf;
		outV->physicalDevice = phyDev;
		outV->device = dev;
		outV->graphicsQueue = gfxQueue;
		outV->commandPool = cmdPool;

		VkSemaphoreCreateInfo semaphoreInfo = {};
		semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

		vkCreateSemaphore(dev, &semaphoreInfo, nullptr, &outV->imageAvailableSemaphore);
		vkCreateSemaphore(dev, &semaphoreInfo, nullptr, &outV->renderFinishedSemaphore);

		FillCommandBuffers(&outV->swapChain, outV->renderPass, outV->graphicsPipeline, outV->pipelineLayout, outV->descriptorSet, outV->vertBuf.buf, outV->indexBuf.buf, vertices, indices);

		return true;
	}
}



int main()
{
	vk::VulkanWindow vkWindow;

	glfwInit();

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	GLFWwindow *window = glfwCreateWindow(1024, 768, "SDFMod", nullptr, nullptr);
	vk::InitVulkan(window, &vkWindow);

	while (!glfwWindowShouldClose(window))
	{
		glfwPollEvents();

		{
			static auto startTime = std::chrono::high_resolution_clock::now();

			auto currentTime = std::chrono::high_resolution_clock::now();
			float time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();

			vk::vertex::UniformBufferObject ubo = {};
			ubo.model = glm::rotate(glm::mat4(1.0f), time * glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
			ubo.view = glm::lookAt(glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
			ubo.proj = glm::perspective(glm::radians(45.0f), vkWindow.swapChain.extent.width / (float)vkWindow.swapChain.extent.height, 0.1f, 10.0f);
			ubo.proj[1][1] *= -1;

			void* data;
			vkMapMemory(vkWindow.device, vkWindow.uniformBuf.mem, 0, sizeof(ubo), 0, &data);
			memcpy(data, &ubo, sizeof(ubo));
			vkUnmapMemory(vkWindow.device, vkWindow.uniformBuf.mem);
		}

		{
			uint32_t imageIndex;
			VkResult result = vkAcquireNextImageKHR(vkWindow.device, vkWindow.swapChain.swapChain, std::numeric_limits<uint64_t>::max(), vkWindow.imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);

			if (result == VK_ERROR_OUT_OF_DATE_KHR) 
			{
				//recreateSwapChain();
				//return;
			}
			else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
				//throw std::runtime_error("failed to acquire swap chain image!");
			}

			VkSubmitInfo submitInfo = {};
			submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

			VkSemaphore waitSemaphores[] = { vkWindow.imageAvailableSemaphore };
			VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
			submitInfo.waitSemaphoreCount = 1;
			submitInfo.pWaitSemaphores = waitSemaphores;
			submitInfo.pWaitDstStageMask = waitStages;

			submitInfo.commandBufferCount = 1;
			submitInfo.pCommandBuffers = &vkWindow.swapChain.commandBuffers[imageIndex];

			VkSemaphore signalSemaphores[] = { vkWindow.renderFinishedSemaphore };
			submitInfo.signalSemaphoreCount = 1;
			submitInfo.pSignalSemaphores = signalSemaphores;

			vkQueueSubmit(vkWindow.graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);

			VkPresentInfoKHR presentInfo = {};
			presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

			presentInfo.waitSemaphoreCount = 1;
			presentInfo.pWaitSemaphores = signalSemaphores;

			VkSwapchainKHR swapChains[] = { vkWindow.swapChain.swapChain };
			presentInfo.swapchainCount = 1;
			presentInfo.pSwapchains = swapChains;

			presentInfo.pImageIndices = &imageIndex;

			result = vkQueuePresentKHR(vkWindow.presentQueue, &presentInfo);

			if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
				//recreateSwapChain();
			}
			else if (result != VK_SUCCESS) {
				throw std::runtime_error("failed to present swap chain image!");
			}

			vkQueueWaitIdle(vkWindow.presentQueue);
		}
	}

	//auto vkDestroyDebugReportCallback = (PFN_vkDestroyDebugReportCallbackEXT)vkGetInstanceProcAddr(vkInst, "vkDestroyDebugReportCallbackEXT");
	//vkDestroyDebugReportCallback(vkInst, vkDbg, nullptr);
	//vkDestroyInstance(vkInst, nullptr);
	glfwDestroyWindow(window);
	glfwTerminate();

	return 0;
}
