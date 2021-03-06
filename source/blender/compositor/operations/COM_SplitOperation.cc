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
 * Copyright 2011, Blender Foundation.
 */

#include "COM_SplitOperation.h"
#include "BKE_image.h"
#include "BLI_listbase.h"
#include "BLI_math_color.h"
#include "BLI_math_vector.h"
#include "BLI_utildefines.h"
#include "MEM_guardedalloc.h"

#include "IMB_imbuf.h"
#include "IMB_imbuf_types.h"

namespace blender::compositor {

SplitOperation::SplitOperation()
{
  this->addInputSocket(DataType::Color);
  this->addInputSocket(DataType::Color);
  this->addOutputSocket(DataType::Color);
  this->m_image1Input = nullptr;
  this->m_image2Input = nullptr;
}

void SplitOperation::initExecution()
{
  /* When initializing the tree during initial load the width and height can be zero. */
  this->m_image1Input = getInputSocketReader(0);
  this->m_image2Input = getInputSocketReader(1);
}

void SplitOperation::deinitExecution()
{
  this->m_image1Input = nullptr;
  this->m_image2Input = nullptr;
}

void SplitOperation::executePixelSampled(float output[4],
                                         float x,
                                         float y,
                                         PixelSampler /*sampler*/)
{
  int perc = this->m_xSplit ? this->m_splitPercentage * this->getWidth() / 100.0f :
                              this->m_splitPercentage * this->getHeight() / 100.0f;
  bool image1 = this->m_xSplit ? x > perc : y > perc;
  if (image1) {
    this->m_image1Input->readSampled(output, x, y, PixelSampler::Nearest);
  }
  else {
    this->m_image2Input->readSampled(output, x, y, PixelSampler::Nearest);
  }
}

void SplitOperation::determineResolution(unsigned int resolution[2],
                                         unsigned int preferredResolution[2])
{
  unsigned int tempPreferredResolution[2] = {0, 0};
  unsigned int tempResolution[2];

  this->getInputSocket(0)->determineResolution(tempResolution, tempPreferredResolution);
  this->setResolutionInputSocketIndex((tempResolution[0] && tempResolution[1]) ? 0 : 1);

  NodeOperation::determineResolution(resolution, preferredResolution);
}

void SplitOperation::update_memory_buffer_partial(MemoryBuffer *output,
                                                  const rcti &area,
                                                  Span<MemoryBuffer *> inputs)
{
  const int percent = this->m_xSplit ? this->m_splitPercentage * this->getWidth() / 100.0f :
                                       this->m_splitPercentage * this->getHeight() / 100.0f;
  const size_t elem_bytes = COM_data_type_bytes_len(getOutputSocket()->getDataType());
  for (BuffersIterator<float> it = output->iterate_with(inputs, area); !it.is_end(); ++it) {
    const bool is_image1 = this->m_xSplit ? it.x > percent : it.y > percent;
    memcpy(it.out, it.in(is_image1 ? 0 : 1), elem_bytes);
  }
}

}  // namespace blender::compositor
