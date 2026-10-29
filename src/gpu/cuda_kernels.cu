// SPDX-License-Identifier: GPL-3.0-or-later
// Rebuild the embedded PTX with tools/compile-cuda-kernels.py. No vendor headers needed.
extern "C" __global__ void rgba_to_planar(const unsigned char *src, float *dst, unsigned pixels, unsigned format)
{
	unsigned i = blockIdx.x * blockDim.x + threadIdx.x;
	if (i >= pixels * 3)
		return;
	unsigned c = i / pixels, p = i % pixels;
	dst[i] = format == 0   ? src[p * 4 + c] / 255.0f
		 : format == 1 ? src[p * 4 + (c == 0   ? 2
					      : c == 2 ? 0
						       : 1)] /
					 255.0f
			       : reinterpret_cast<const float *>(src)[p * 4 + c];
}
extern "C" __global__ void planar_to_rgba(const float *rgb, const float *alpha, float *dst, unsigned pixels)
{
	unsigned i = blockIdx.x * blockDim.x + threadIdx.x;
	if (i >= pixels * 4)
		return;
	unsigned c = i % 4, p = i / 4;
	dst[i] = c == 3 ? alpha[p] : rgb[c * pixels + p];
}
extern "C" __global__ void resize_area(const float *src, float *dst, unsigned w, unsigned h, unsigned ow, unsigned oh,
				       unsigned channels)
{
	unsigned i = blockIdx.x * blockDim.x + threadIdx.x;
	if (i >= ow * oh * channels)
		return;
	unsigned c = i / (ow * oh), y = i / ow % oh, x = i % ow;
	unsigned x0 = x * w / ow, x1 = ((x + 1) * w + ow - 1) / ow;
	unsigned y0 = y * h / oh, y1 = ((y + 1) * h + oh - 1) / oh;
	float v = 0;
	for (unsigned yy = y0; yy < y1; ++yy)
		for (unsigned xx = x0; xx < x1; ++xx)
			v += src[(c * h + yy) * w + xx];
	dst[i] = v / ((y1 - y0) * (x1 - x0));
}
extern "C" __global__ void validate(const float *src, unsigned count, unsigned unit, unsigned *invalid)
{
	unsigned i = blockIdx.x * blockDim.x + threadIdx.x;
	if (i < count) {
		float v = src[i];
		if (!isfinite(v) || (unit && (v < 0 || v > 1)))
			atomicExch(invalid, 1U);
	}
}
extern "C" __global__ void unpack_atlas(const float *src, float *dst, unsigned budget)
{
	unsigned i = blockIdx.x * blockDim.x + threadIdx.x;
	if (i >= budget * 11 * 52 * 52)
		return;
	unsigned p = i % 2704, c = i / 2704 % 11, tile = i / (2704 * 11);
	unsigned x = (tile % 16) * 52 + p % 52;
	unsigned y = (c / 4) * ((budget + 15) / 16 * 52) + tile / 16 * 52 + p / 52;
	float v = src[(y * 832 + x) * 4 + c % 4];
	unsigned short half;
	asm("cvt.rn.f16.f32 %0, %1;" : "=h"(half) : "f"(v));
	asm("cvt.f32.f16 %0, %1;" : "=f"(v) : "h"(half));
	dst[i] = v;
}
extern "C" __global__ void pack_centres(const float *rgb, const float *alpha, float *dst, unsigned budget)
{
	unsigned i = blockIdx.x * blockDim.x + threadIdx.x;
	if (i >= budget * 1024 * 4)
		return;
	unsigned c = i % 4, x = i / 4 % 512, y = i / (4 * 512);
	unsigned tile = y / 32 * 16 + x / 32, p = (y % 32 + 10) * 52 + x % 32 + 10;
	dst[i] = c == 3 ? alpha[tile * 2704 + p] : rgb[(tile * 3 + c) * 2704 + p];
}
