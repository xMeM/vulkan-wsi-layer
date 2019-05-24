/*
 * Copyright (c) 2018-2019 Arm Limited.
 *
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <cassert>
#include <map>
#include <mutex>

#include "private_data.hpp"

using scoped_mutex = std::lock_guard<std::mutex>;

namespace layer
{

static std::mutex g_data_lock;
static std::map<void *, instance_private_data *> g_instance_data;
static std::map<void *, device_private_data *> g_device_data;

instance_private_data::instance_private_data(VkInstance inst, PFN_vkGetInstanceProcAddr get_proc,
                                             PFN_vkSetInstanceLoaderData set_loader_data)
   : disp(inst, get_proc)
   , SetInstanceLoaderData(set_loader_data)
{
}

instance_private_data &instance_private_data::create(VkInstance inst, PFN_vkGetInstanceProcAddr get_proc,
                                                     PFN_vkSetInstanceLoaderData set_loader_data)
{
   instance_private_data *inst_data = new instance_private_data(inst, get_proc, set_loader_data);
   scoped_mutex lock(g_data_lock);
   g_instance_data[get_key(inst)] = inst_data;
   return *inst_data;
}

instance_private_data &instance_private_data::get(void *key)
{
   scoped_mutex lock(g_data_lock);
   instance_private_data *data = g_instance_data[key];
   assert(data);
   return *data;
}

void instance_private_data::destroy(VkInstance inst)
{
   instance_private_data *data;
   {
      scoped_mutex lock(g_data_lock);
      data = g_instance_data[get_key(inst)];
      assert(data);
      g_instance_data.erase(get_key(inst));
   }
   delete data;
}

device_private_data::device_private_data(VkDevice dev, PFN_vkGetDeviceProcAddr get_proc,
                                         instance_private_data &inst_data, PFN_vkSetDeviceLoaderData set_loader_data)
   : disp(dev, get_proc)
   , instance_data(inst_data)
   , SetDeviceLoaderData(set_loader_data)
{
}

device_private_data &device_private_data::create(VkDevice dev, PFN_vkGetDeviceProcAddr get_proc,
                                                 VkPhysicalDevice phys_dev, PFN_vkSetDeviceLoaderData set_loader_data)
{
   device_private_data *dev_data =
      new device_private_data(dev, get_proc, instance_private_data::get(get_key(phys_dev)), set_loader_data);
   scoped_mutex lock(g_data_lock);
   g_device_data[get_key(dev)] = dev_data;
}

device_private_data &device_private_data::get(void *key)
{
   scoped_mutex lock(g_data_lock);
   device_private_data *data = g_device_data[key];
   assert(data);
   return *data;
}

void device_private_data::destroy(VkDevice dev)
{
   device_private_data *data;
   {
      scoped_mutex lock(g_data_lock);
      data = g_device_data[get_key(dev)];
      g_device_data.erase(get_key(dev));
   }
   delete data;
}

} /* namespace layer */
