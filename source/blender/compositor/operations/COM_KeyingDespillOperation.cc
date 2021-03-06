/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * Copyright 2012, Blender Foundation.
 */

#include "COM_KeyingDespillOperation.h"

#include "MEM_guardedalloc.h"

#include "BLI_listbase.h"
#include "BLI_math.h"

namespace blender::compositor {

KeyingDespillOperation::KeyingDespillOperation()
{
  this->addInputSocket(DataType::Color);
  this->addInputSocket(DataType::Color);
  this->addOutputSocket(DataType::Color);

  this->m_despillFactor = 0.5f;
  this->m_colorBalance = 0.5f;

  this->m_pixelReader = nullptr;
  this->m_screenReader = nullptr;
  flags.can_be_constant = true;
}

void KeyingDespillOperation::initExecution()
{
  this->m_pixelReader = this->getInputSocketReader(0);
  this->m_screenReader = this->getInputSocketReader(1);
}

void KeyingDespillOperation::deinitExecution()
{
  this->m_pixelReader = nullptr;
  this->m_screenReader = nullptr;
}

void KeyingDespillOperation::executePixelSampled(float output[4],
                                                 float x,
                                                 float y,
                                                 PixelSampler sampler)
{
  float pixelColor[4];
  float screenColor[4];

  this->m_pixelReader->readSampled(pixelColor, x, y, sampler);
  this->m_screenReader->readSampled(screenColor, x, y, sampler);

  const int screen_primary_channel = max_axis_v3(screenColor);
  const int other_1 = (screen_primary_channel + 1) % 3;
  const int other_2 = (screen_primary_channel + 2) % 3;

  const int min_channel = MIN2(other_1, other_2);
  const int max_channel = MAX2(other_1, other_2);

  float average_value, amount;

  average_value = this->m_colorBalance * pixelColor[min_channel] +
                  (1.0f - this->m_colorBalance) * pixelColor[max_channel];
  amount = (pixelColor[screen_primary_channel] - average_value);

  copy_v4_v4(output, pixelColor);

  const float amount_despill = this->m_despillFactor * amount;
  if (amount_despill > 0.0f) {
    output[screen_primary_channel] = pixelColor[screen_primary_channel] - amount_despill;
  }
}

void KeyingDespillOperation::update_memory_buffer_partial(MemoryBuffer *output,
                                                          const rcti &area,
                                                          Span<MemoryBuffer *> inputs)
{
  for (BuffersIterator<float> it = output->iterate_with(inputs, area); !it.is_end(); ++it) {
    const float *pixel_color = it.in(0);
    const float *screen_color = it.in(1);

    const int screen_primary_channel = max_axis_v3(screen_color);
    const int other_1 = (screen_primary_channel + 1) % 3;
    const int other_2 = (screen_primary_channel + 2) % 3;

    const int min_channel = MIN2(other_1, other_2);
    const int max_channel = MAX2(other_1, other_2);

    const float average_value = m_colorBalance * pixel_color[min_channel] +
                                (1.0f - m_colorBalance) * pixel_color[max_channel];
    const float amount = (pixel_color[screen_primary_channel] - average_value);

    copy_v4_v4(it.out, pixel_color);

    const float amount_despill = m_despillFactor * amount;
    if (amount_despill > 0.0f) {
      it.out[screen_primary_channel] = pixel_color[screen_primary_channel] - amount_despill;
    }
  }
}

}  // namespace blender::compositor
