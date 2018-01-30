// Copyright (c) 2016- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#pragma once

#include <tuple>
#include <map>

#include "Common/Vulkan/VulkanContext.h"
#include "Common/Vulkan/VulkanLoader.h"
#include "Common/Vulkan/VulkanImage.h"

// Vulkan doesn't really have the concept of an FBO that owns the images,
// but it does have the concept of a framebuffer as a set of attachments.
// VulkanFBO is an approximation of the FBO concept the other backends use
// to make things as similar as possible without being suboptimal.
//
// An FBO can be rendered to and used as a texture multiple times in a frame.
// Even at multiple sizes, while keeping the same contents.
// With GL or D3D we'd just rely on the driver managing duplicates for us, but in
// Vulkan we will want to be able to batch up the whole frame and reorder passes
// so that all textures are ready before the main scene, instead of switching back and
// forth. This comes at a memory cost but will be worth it.
//
// When we render to a scene, then render to a texture, then go back to the scene and
// use that texture, we will register that as a dependency. Then we will walk the DAG
// to find the final order of command buffers, and execute it.
//
// Each FBO will get its own command buffer for each pass. 

// Similar to a subset of Thin3D, but separate.
// This is used for things like postprocessing shaders, depal, etc.
// No UBO data is used, only PushConstants.
// No transform matrices, only post-proj coordinates.
// Two textures can be sampled.
// Some simplified depth/stencil modes available.

class Vulkan2D {
public:
	Vulkan2D(VulkanContext *vulkan);
	~Vulkan2D();

	VulkanContext *GetVulkanContext() const { return vulkan_; }

	void DeviceLost();
	void DeviceRestore(VulkanContext *vulkan);
	void Shutdown();

	enum class VK2DDepthStencilMode {
		NONE,
		STENCIL_REPLACE_ALWAYS,  // Does not draw to color.
	};

	// The only supported primitive is the triangle strip, for simplicity.
	// ReadVertices can be used for vertex-less rendering where you generate verts in the vshader.
	VkPipeline GetPipeline(VkRenderPass rp, VkShaderModule vs, VkShaderModule fs, bool readVertices = true, VK2DDepthStencilMode depthStencilMode = VK2DDepthStencilMode::NONE);
	VkPipelineLayout GetPipelineLayout() const { return pipelineLayout_; }
	void BeginFrame();
	void EndFrame();

	VkDescriptorSet GetDescriptorSet(VkImageView tex1, VkSampler sampler1, VkImageView tex2, VkSampler sampler2);

	struct Vertex {
		float x, y, z;
		float u, v;
	};

private:
	void InitDeviceObjects();
	void DestroyDeviceObjects();

	VulkanContext *vulkan_ = nullptr;
	VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;
	VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
	VkPipelineCache pipelineCache_ = VK_NULL_HANDLE;

	// Yes, another one...
	struct DescriptorSetKey {
		VkImageView imageView[2];
		VkSampler sampler[2];

		bool operator < (const DescriptorSetKey &other) const {
			return std::tie(imageView[0], imageView[1], sampler[0], sampler[1]) <
				std::tie(other.imageView[0], other.imageView[1], other.sampler[0], other.sampler[1]);
		}
	};

	struct PipelineKey {
		VkShaderModule vs;
		VkShaderModule fs;
		VkRenderPass rp;
		VK2DDepthStencilMode depthStencilMode;
		bool readVertices;
		bool operator < (const PipelineKey &other) const {
			return std::tie(vs, fs, rp, depthStencilMode, readVertices) < std::tie(other.vs, other.fs, other.rp, other.depthStencilMode, other.readVertices);
		}
	};

	struct FrameData {
		VkDescriptorPool descPool;
		std::map<DescriptorSetKey, VkDescriptorSet> descSets;
	};

	FrameData frameData_[VulkanContext::MAX_INFLIGHT_FRAMES];

	std::map<PipelineKey, VkPipeline> pipelines_;
};


VkShaderModule CompileShaderModule(VulkanContext *vulkan, VkShaderStageFlagBits stage, const char *code, std::string *error);
