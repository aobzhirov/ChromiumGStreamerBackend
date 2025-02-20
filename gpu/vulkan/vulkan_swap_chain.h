// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GPU_VULKAN_VULKAN_SWAP_CHAIN_H_
#define GPU_VULKAN_VULKAN_SWAP_CHAIN_H_

#include <vulkan/vulkan.h>
#include <vector>

#include "base/memory/scoped_ptr.h"
#include "gpu/vulkan/vulkan_export.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/swap_result.h"

namespace gpu {

class VulkanCommandBuffer;
class VulkanCommandPool;
class VulkanDeviceQueue;
class VulkanImageView;

class VulkanSwapChain {
 public:
  VulkanSwapChain();
  ~VulkanSwapChain();

  bool Initialize(VulkanDeviceQueue* device_queue,
                  VkSurfaceKHR surface,
                  const VkSurfaceCapabilitiesKHR& surface_caps,
                  const VkSurfaceFormatKHR& surface_format);
  void Destroy();

  gfx::SwapResult SwapBuffers();

  uint32_t num_images() const { return static_cast<uint32_t>(images_.size()); }
  uint32_t current_image() const { return current_image_; }
  const gfx::Size& size() const { return size_; }

  VulkanImageView* GetImageView(uint32_t index) const {
    DCHECK_LT(index, images_.size());
    return images_[index]->image_view.get();
  }

  VulkanImageView* GetCurrentImageView() const {
    return GetImageView(current_image_);
  }

  VulkanCommandBuffer* GetCurrentCommandBuffer() const {
    DCHECK_LT(current_image_, images_.size());
    return images_[current_image_]->command_buffer.get();
  }

 private:
  bool InitializeSwapChain(VkSurfaceKHR surface,
                           const VkSurfaceCapabilitiesKHR& surface_caps,
                           const VkSurfaceFormatKHR& surface_format);
  void DestroySwapChain();

  bool InitializeSwapImages(const VkSurfaceCapabilitiesKHR& surface_caps,
                            const VkSurfaceFormatKHR& surface_format);
  void DestroySwapImages();

  VulkanDeviceQueue* device_queue_;
  VkSwapchainKHR swap_chain_ = VK_NULL_HANDLE;

  scoped_ptr<VulkanCommandPool> command_pool_;

  gfx::Size size_;

  struct ImageData {
    ImageData();
    ~ImageData();

    VkImage image = VK_NULL_HANDLE;
    scoped_ptr<VulkanImageView> image_view;
    scoped_ptr<VulkanCommandBuffer> command_buffer;

    VkSemaphore render_semaphore = VK_NULL_HANDLE;
    VkSemaphore present_semaphore = VK_NULL_HANDLE;
  };
  std::vector<scoped_ptr<ImageData>> images_;
  uint32_t current_image_ = 0;

  VkSemaphore next_present_semaphore_ = VK_NULL_HANDLE;
};

}  // namespace gpu

#endif  // GPU_VULKAN_VULKAN_SWAP_CHAIN_H_
