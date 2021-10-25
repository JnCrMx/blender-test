/*
 * Adapted from Open Shading Language with this license:
 *
 * Copyright (c) 2009-2010 Sony Pictures Imageworks Inc., et al.
 * All Rights Reserved.
 *
 * Modifications Copyright 2013, Blender Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * * Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 * * Neither the name of Sony Pictures Imageworks nor the names of its
 *   contributors may be used to endorse or promote products derived from
 *   this software without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

CCL_NAMESPACE_BEGIN

/* Julia Node */

#define csquare(a) make_float2(a.x*a.x-a.y*a.y, a.x*a.y+a.y*a.x)
#define len2(v) sqrt(dot(v, v))

ccl_device float svm_julia(float2 p, float2 c, int maxiter, float breakout, bool smooth)
{
    float2 x = p;
    float breakoutsquared = breakout*breakout;

    for(int i=0; i<maxiter; i++)
    {
        x = csquare(x) + c;
        if(dot(x, x) > breakoutsquared)
        {
            if(smooth)
                return (float)(i) + 1.0f - log(log2(len2(x)));
            else
                return (float)i;
        }
    }

    return (float)(maxiter);
}

ccl_device void svm_node_tex_julia(KernelGlobals *kg, ShaderData *sd, float *stack, uint4 node)
{
  uint co_offset, rule_offset, iterations, smooth;
  uint color_offset, _3;

  svm_unpack_node_uchar4(node.y, &co_offset, &rule_offset, &iterations, &smooth);
  svm_unpack_node_uchar2(node.z, &color_offset, &_3);

  float3 co = stack_load_float3(stack, co_offset);
  float3 rule = stack_load_float3(stack, rule_offset);
  float breakout = __uint_as_float(node.w);

  float it = svm_julia(make_float2(co.x, co.y), make_float2(rule.x, rule.y), iterations, breakout, smooth);

  if (stack_valid(color_offset))
    stack_store_float3(stack, color_offset, hsv_to_rgb(make_float3(it/iterations, 1.0f, it < iterations ? 1.0f : 0.0f)));
}

CCL_NAMESPACE_END
