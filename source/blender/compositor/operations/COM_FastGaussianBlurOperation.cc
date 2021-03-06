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

#include <climits>

#include "BLI_utildefines.h"
#include "COM_FastGaussianBlurOperation.h"
#include "MEM_guardedalloc.h"

namespace blender::compositor {

FastGaussianBlurOperation::FastGaussianBlurOperation() : BlurBaseOperation(DataType::Color)
{
  this->m_iirgaus = nullptr;
}

void FastGaussianBlurOperation::executePixel(float output[4], int x, int y, void *data)
{
  MemoryBuffer *newData = (MemoryBuffer *)data;
  newData->read(output, x, y);
}

bool FastGaussianBlurOperation::determineDependingAreaOfInterest(
    rcti * /*input*/, ReadBufferOperation *readOperation, rcti *output)
{
  rcti newInput;
  rcti sizeInput;
  sizeInput.xmin = 0;
  sizeInput.ymin = 0;
  sizeInput.xmax = 5;
  sizeInput.ymax = 5;

  NodeOperation *operation = this->getInputOperation(1);
  if (operation->determineDependingAreaOfInterest(&sizeInput, readOperation, output)) {
    return true;
  }

  if (this->m_iirgaus) {
    return false;
  }

  newInput.xmin = 0;
  newInput.ymin = 0;
  newInput.xmax = this->getWidth();
  newInput.ymax = this->getHeight();

  return NodeOperation::determineDependingAreaOfInterest(&newInput, readOperation, output);
}

void FastGaussianBlurOperation::init_data()
{
  BlurBaseOperation::init_data();
  this->m_sx = this->m_data.sizex * this->m_size / 2.0f;
  this->m_sy = this->m_data.sizey * this->m_size / 2.0f;
}

void FastGaussianBlurOperation::initExecution()
{
  BlurBaseOperation::initExecution();
  BlurBaseOperation::initMutex();
}

void FastGaussianBlurOperation::deinitExecution()
{
  if (this->m_iirgaus) {
    delete this->m_iirgaus;
    this->m_iirgaus = nullptr;
  }
  BlurBaseOperation::deinitMutex();
}

void *FastGaussianBlurOperation::initializeTileData(rcti *rect)
{
  lockMutex();
  if (!this->m_iirgaus) {
    MemoryBuffer *newBuf = (MemoryBuffer *)this->m_inputProgram->initializeTileData(rect);
    MemoryBuffer *copy = new MemoryBuffer(*newBuf);
    updateSize();

    int c;
    this->m_sx = this->m_data.sizex * this->m_size / 2.0f;
    this->m_sy = this->m_data.sizey * this->m_size / 2.0f;

    if ((this->m_sx == this->m_sy) && (this->m_sx > 0.0f)) {
      for (c = 0; c < COM_DATA_TYPE_COLOR_CHANNELS; c++) {
        IIR_gauss(copy, this->m_sx, c, 3);
      }
    }
    else {
      if (this->m_sx > 0.0f) {
        for (c = 0; c < COM_DATA_TYPE_COLOR_CHANNELS; c++) {
          IIR_gauss(copy, this->m_sx, c, 1);
        }
      }
      if (this->m_sy > 0.0f) {
        for (c = 0; c < COM_DATA_TYPE_COLOR_CHANNELS; c++) {
          IIR_gauss(copy, this->m_sy, c, 2);
        }
      }
    }
    this->m_iirgaus = copy;
  }
  unlockMutex();
  return this->m_iirgaus;
}

void FastGaussianBlurOperation::IIR_gauss(MemoryBuffer *src,
                                          float sigma,
                                          unsigned int chan,
                                          unsigned int xy)
{
  BLI_assert(!src->is_a_single_elem());
  double q, q2, sc, cf[4], tsM[9], tsu[3], tsv[3];
  double *X, *Y, *W;
  const unsigned int src_width = src->getWidth();
  const unsigned int src_height = src->getHeight();
  unsigned int x, y, sz;
  unsigned int i;
  float *buffer = src->getBuffer();
  const uint8_t num_channels = src->get_num_channels();

  /* <0.5 not valid, though can have a possibly useful sort of sharpening effect. */
  if (sigma < 0.5f) {
    return;
  }

  if ((xy < 1) || (xy > 3)) {
    xy = 3;
  }

  /* XXX The YVV macro defined below explicitly expects sources of at least 3x3 pixels,
   *     so just skipping blur along faulty direction if src's def is below that limit! */
  if (src_width < 3) {
    xy &= ~1;
  }
  if (src_height < 3) {
    xy &= ~2;
  }
  if (xy < 1) {
    return;
  }

  /* See "Recursive Gabor Filtering" by Young/VanVliet
   * all factors here in double-precision.
   * Required, because for single-precision floating point seems to blow up if `sigma > ~200`. */
  if (sigma >= 3.556f) {
    q = 0.9804f * (sigma - 3.556f) + 2.5091f;
  }
  else { /* `sigma >= 0.5`. */
    q = (0.0561f * sigma + 0.5784f) * sigma - 0.2568f;
  }
  q2 = q * q;
  sc = (1.1668 + q) * (3.203729649 + (2.21566 + q) * q);
  /* No gabor filtering here, so no complex multiplies, just the regular coefficients.
   * all negated here, so as not to have to recalc Triggs/Sdika matrix. */
  cf[1] = q * (5.788961737 + (6.76492 + 3.0 * q) * q) / sc;
  cf[2] = -q2 * (3.38246 + 3.0 * q) / sc;
  /* 0 & 3 unchanged. */
  cf[3] = q2 * q / sc;
  cf[0] = 1.0 - cf[1] - cf[2] - cf[3];

  /* Triggs/Sdika border corrections,
   * it seems to work, not entirely sure if it is actually totally correct,
   * Besides J.M.Geusebroek's `anigauss.c` (see http://www.science.uva.nl/~mark),
   * found one other implementation by Cristoph Lampert,
   * but neither seem to be quite the same, result seems to be ok so far anyway.
   * Extra scale factor here to not have to do it in filter,
   * though maybe this had something to with the precision errors */
  sc = cf[0] / ((1.0 + cf[1] - cf[2] + cf[3]) * (1.0 - cf[1] - cf[2] - cf[3]) *
                (1.0 + cf[2] + (cf[1] - cf[3]) * cf[3]));
  tsM[0] = sc * (-cf[3] * cf[1] + 1.0 - cf[3] * cf[3] - cf[2]);
  tsM[1] = sc * ((cf[3] + cf[1]) * (cf[2] + cf[3] * cf[1]));
  tsM[2] = sc * (cf[3] * (cf[1] + cf[3] * cf[2]));
  tsM[3] = sc * (cf[1] + cf[3] * cf[2]);
  tsM[4] = sc * (-(cf[2] - 1.0) * (cf[2] + cf[3] * cf[1]));
  tsM[5] = sc * (-(cf[3] * cf[1] + cf[3] * cf[3] + cf[2] - 1.0) * cf[3]);
  tsM[6] = sc * (cf[3] * cf[1] + cf[2] + cf[1] * cf[1] - cf[2] * cf[2]);
  tsM[7] = sc * (cf[1] * cf[2] + cf[3] * cf[2] * cf[2] - cf[1] * cf[3] * cf[3] -
                 cf[3] * cf[3] * cf[3] - cf[3] * cf[2] + cf[3]);
  tsM[8] = sc * (cf[3] * (cf[1] + cf[3] * cf[2]));

#define YVV(L) \
  { \
    W[0] = cf[0] * X[0] + cf[1] * X[0] + cf[2] * X[0] + cf[3] * X[0]; \
    W[1] = cf[0] * X[1] + cf[1] * W[0] + cf[2] * X[0] + cf[3] * X[0]; \
    W[2] = cf[0] * X[2] + cf[1] * W[1] + cf[2] * W[0] + cf[3] * X[0]; \
    for (i = 3; i < L; i++) { \
      W[i] = cf[0] * X[i] + cf[1] * W[i - 1] + cf[2] * W[i - 2] + cf[3] * W[i - 3]; \
    } \
    tsu[0] = W[L - 1] - X[L - 1]; \
    tsu[1] = W[L - 2] - X[L - 1]; \
    tsu[2] = W[L - 3] - X[L - 1]; \
    tsv[0] = tsM[0] * tsu[0] + tsM[1] * tsu[1] + tsM[2] * tsu[2] + X[L - 1]; \
    tsv[1] = tsM[3] * tsu[0] + tsM[4] * tsu[1] + tsM[5] * tsu[2] + X[L - 1]; \
    tsv[2] = tsM[6] * tsu[0] + tsM[7] * tsu[1] + tsM[8] * tsu[2] + X[L - 1]; \
    Y[L - 1] = cf[0] * W[L - 1] + cf[1] * tsv[0] + cf[2] * tsv[1] + cf[3] * tsv[2]; \
    Y[L - 2] = cf[0] * W[L - 2] + cf[1] * Y[L - 1] + cf[2] * tsv[0] + cf[3] * tsv[1]; \
    Y[L - 3] = cf[0] * W[L - 3] + cf[1] * Y[L - 2] + cf[2] * Y[L - 1] + cf[3] * tsv[0]; \
    /* 'i != UINT_MAX' is really 'i >= 0', but necessary for unsigned int wrapping */ \
    for (i = L - 4; i != UINT_MAX; i--) { \
      Y[i] = cf[0] * W[i] + cf[1] * Y[i + 1] + cf[2] * Y[i + 2] + cf[3] * Y[i + 3]; \
    } \
  } \
  (void)0

  /* Intermediate buffers. */
  sz = MAX2(src_width, src_height);
  X = (double *)MEM_callocN(sz * sizeof(double), "IIR_gauss X buf");
  Y = (double *)MEM_callocN(sz * sizeof(double), "IIR_gauss Y buf");
  W = (double *)MEM_callocN(sz * sizeof(double), "IIR_gauss W buf");
  if (xy & 1) { /* H. */
    int offset;
    for (y = 0; y < src_height; y++) {
      const int yx = y * src_width;
      offset = yx * num_channels + chan;
      for (x = 0; x < src_width; x++) {
        X[x] = buffer[offset];
        offset += num_channels;
      }
      YVV(src_width);
      offset = yx * num_channels + chan;
      for (x = 0; x < src_width; x++) {
        buffer[offset] = Y[x];
        offset += num_channels;
      }
    }
  }
  if (xy & 2) { /* V. */
    int offset;
    const int add = src_width * num_channels;

    for (x = 0; x < src_width; x++) {
      offset = x * num_channels + chan;
      for (y = 0; y < src_height; y++) {
        X[y] = buffer[offset];
        offset += add;
      }
      YVV(src_height);
      offset = x * num_channels + chan;
      for (y = 0; y < src_height; y++) {
        buffer[offset] = Y[y];
        offset += add;
      }
    }
  }

  MEM_freeN(X);
  MEM_freeN(W);
  MEM_freeN(Y);
#undef YVV
}

void FastGaussianBlurOperation::get_area_of_interest(const int input_idx,
                                                     const rcti &output_area,
                                                     rcti &r_input_area)
{
  switch (input_idx) {
    case IMAGE_INPUT_INDEX:
      r_input_area.xmin = 0;
      r_input_area.xmax = getWidth();
      r_input_area.ymin = 0;
      r_input_area.ymax = getHeight();
      break;
    default:
      BlurBaseOperation::get_area_of_interest(input_idx, output_area, r_input_area);
      return;
  }
}

void FastGaussianBlurOperation::update_memory_buffer_started(MemoryBuffer *output,
                                                             const rcti &area,
                                                             Span<MemoryBuffer *> inputs)
{
  /* TODO(manzanilla): Add a render test and make #IIR_gauss multi-threaded with support for
   * an output buffer. */
  const MemoryBuffer *input = inputs[IMAGE_INPUT_INDEX];
  MemoryBuffer *image = nullptr;
  const bool is_full_output = BLI_rcti_compare(&output->get_rect(), &area);
  if (is_full_output) {
    image = output;
  }
  else {
    image = new MemoryBuffer(getOutputSocket()->getDataType(), area);
  }
  image->copy_from(input, area);

  if ((this->m_sx == this->m_sy) && (this->m_sx > 0.0f)) {
    for (const int c : IndexRange(COM_DATA_TYPE_COLOR_CHANNELS)) {
      IIR_gauss(image, this->m_sx, c, 3);
    }
  }
  else {
    if (this->m_sx > 0.0f) {
      for (const int c : IndexRange(COM_DATA_TYPE_COLOR_CHANNELS)) {
        IIR_gauss(image, this->m_sx, c, 1);
      }
    }
    if (this->m_sy > 0.0f) {
      for (const int c : IndexRange(COM_DATA_TYPE_COLOR_CHANNELS)) {
        IIR_gauss(image, this->m_sy, c, 2);
      }
    }
  }

  if (!is_full_output) {
    output->copy_from(image, area);
    delete image;
  }
}

FastGaussianBlurValueOperation::FastGaussianBlurValueOperation()
{
  this->addInputSocket(DataType::Value);
  this->addOutputSocket(DataType::Value);
  this->m_iirgaus = nullptr;
  this->m_inputprogram = nullptr;
  this->m_sigma = 1.0f;
  this->m_overlay = 0;
  flags.complex = true;
}

void FastGaussianBlurValueOperation::executePixel(float output[4], int x, int y, void *data)
{
  MemoryBuffer *newData = (MemoryBuffer *)data;
  newData->read(output, x, y);
}

bool FastGaussianBlurValueOperation::determineDependingAreaOfInterest(
    rcti * /*input*/, ReadBufferOperation *readOperation, rcti *output)
{
  rcti newInput;

  if (this->m_iirgaus) {
    return false;
  }

  newInput.xmin = 0;
  newInput.ymin = 0;
  newInput.xmax = this->getWidth();
  newInput.ymax = this->getHeight();

  return NodeOperation::determineDependingAreaOfInterest(&newInput, readOperation, output);
}

void FastGaussianBlurValueOperation::initExecution()
{
  this->m_inputprogram = getInputSocketReader(0);
  initMutex();
}

void FastGaussianBlurValueOperation::deinitExecution()
{
  if (this->m_iirgaus) {
    delete this->m_iirgaus;
    this->m_iirgaus = nullptr;
  }
  deinitMutex();
}

void *FastGaussianBlurValueOperation::initializeTileData(rcti *rect)
{
  lockMutex();
  if (!this->m_iirgaus) {
    MemoryBuffer *newBuf = (MemoryBuffer *)this->m_inputprogram->initializeTileData(rect);
    MemoryBuffer *copy = new MemoryBuffer(*newBuf);
    FastGaussianBlurOperation::IIR_gauss(copy, this->m_sigma, 0, 3);

    if (this->m_overlay == FAST_GAUSS_OVERLAY_MIN) {
      float *src = newBuf->getBuffer();
      float *dst = copy->getBuffer();
      for (int i = copy->getWidth() * copy->getHeight(); i != 0;
           i--, src += COM_DATA_TYPE_VALUE_CHANNELS, dst += COM_DATA_TYPE_VALUE_CHANNELS) {
        if (*src < *dst) {
          *dst = *src;
        }
      }
    }
    else if (this->m_overlay == FAST_GAUSS_OVERLAY_MAX) {
      float *src = newBuf->getBuffer();
      float *dst = copy->getBuffer();
      for (int i = copy->getWidth() * copy->getHeight(); i != 0;
           i--, src += COM_DATA_TYPE_VALUE_CHANNELS, dst += COM_DATA_TYPE_VALUE_CHANNELS) {
        if (*src > *dst) {
          *dst = *src;
        }
      }
    }

    this->m_iirgaus = copy;
  }
  unlockMutex();
  return this->m_iirgaus;
}

void FastGaussianBlurValueOperation::get_area_of_interest(const int UNUSED(input_idx),
                                                          const rcti &UNUSED(output_area),
                                                          rcti &r_input_area)
{
  r_input_area.xmin = 0;
  r_input_area.xmax = getWidth();
  r_input_area.ymin = 0;
  r_input_area.ymax = getHeight();
}

void FastGaussianBlurValueOperation::update_memory_buffer_started(MemoryBuffer *UNUSED(output),
                                                                  const rcti &UNUSED(area),
                                                                  Span<MemoryBuffer *> inputs)
{
  if (m_iirgaus == nullptr) {
    const MemoryBuffer *image = inputs[0];
    MemoryBuffer *gauss = new MemoryBuffer(*image);
    FastGaussianBlurOperation::IIR_gauss(gauss, m_sigma, 0, 3);
    m_iirgaus = gauss;
  }
}

void FastGaussianBlurValueOperation::update_memory_buffer_partial(MemoryBuffer *output,
                                                                  const rcti &area,
                                                                  Span<MemoryBuffer *> inputs)
{
  MemoryBuffer *image = inputs[0];
  BuffersIterator<float> it = output->iterate_with({image, m_iirgaus}, area);
  if (this->m_overlay == FAST_GAUSS_OVERLAY_MIN) {
    for (; !it.is_end(); ++it) {
      *it.out = MIN2(*it.in(0), *it.in(1));
    }
  }
  else if (this->m_overlay == FAST_GAUSS_OVERLAY_MAX) {
    for (; !it.is_end(); ++it) {
      *it.out = MAX2(*it.in(0), *it.in(1));
    }
  }
}

}  // namespace blender::compositor
