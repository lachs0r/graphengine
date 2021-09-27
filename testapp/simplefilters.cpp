#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>
#include "simplefilters.h"

#ifdef _MSC_VER
  #include <emmintrin.h>
  static long xlrintf(float x) { return _mm_cvtss_si32(_mm_set_ss(x)); }
  static float xsqrtf(float x) { return _mm_cvtss_f32(_mm_sqrt_ss(_mm_set_ss(x))); }
#else
  #define xlrintf lrintf
  #define xsqrtf sqrtf
#endif

namespace {

class Spatial3x3Filter : public graphengine::Filter {
protected:
	graphengine::FilterDescriptor m_desc = {};
public:
	Spatial3x3Filter(unsigned width, unsigned height)
	{
		m_desc.format.width = width;
		m_desc.format.height = height;
		m_desc.format.bytes_per_sample = 1;

		m_desc.num_deps = 1;
		m_desc.num_planes = 1;
		m_desc.step = 1;
	}

	const graphengine::FilterDescriptor &descriptor() const noexcept override { return m_desc; }

	std::pair<unsigned, unsigned> get_row_deps(unsigned i) const noexcept override
	{
		unsigned topdep = std::max(i, 1U) - 1;
		unsigned bottomdep = static_cast<unsigned>(
			std::min(static_cast<size_t>(i) + 2, static_cast<size_t>(m_desc.format.height)));
		return{ topdep, bottomdep };
	}

	std::pair<unsigned, unsigned> get_col_deps(unsigned left, unsigned right) const noexcept override
	{
		unsigned leftdep = std::max(left, 1U) - 1;
		unsigned rightdep = static_cast<unsigned>(
			std::min(static_cast<size_t>(right) + 2, static_cast<size_t>(m_desc.format.width)));
		return{ leftdep, rightdep };
	}

	void init_context(void *) const noexcept override {}

};

class BoxBlurFilter final : public Spatial3x3Filter {
public:
	using Spatial3x3Filter::Spatial3x3Filter;

	void process(const graphengine::BufferDescriptor *in, const graphengine::BufferDescriptor *out,
	             unsigned i, unsigned left, unsigned right, void *context, void *tmp) const noexcept override
	{
		auto range = get_row_deps(i);
		const uint8_t *y0 = static_cast<const uint8_t *>(in->get_line(range.first));
		const uint8_t *y1 = static_cast<const uint8_t *>(in->get_line(i));
		const uint8_t *y2 = static_cast<const uint8_t *>(in->get_line(range.second - 1));
		uint8_t *dstp = static_cast<uint8_t *>(out->get_line(i));

		for (ptrdiff_t j = left; j < static_cast<ptrdiff_t>(right); ++j) {
			ptrdiff_t x0 = std::max(j - 1, static_cast<ptrdiff_t>(0));
			ptrdiff_t x1 = j;
			ptrdiff_t x2 = std::min(j + 1, static_cast<ptrdiff_t>(m_desc.format.width - 1));

			uint32_t accum = 0;
			accum = y0[x0] + y0[x1] + y0[x2] +
			        y1[x0] + y1[x1] + y1[x2] +
			        y2[x0] + y2[x1] + y2[x2];
			dstp[j] = (accum + 4) / 9;
		}
	}
};

class SobelFilter final : public Spatial3x3Filter {
public:
	using Spatial3x3Filter::Spatial3x3Filter;

	void process(const graphengine::BufferDescriptor *in, const graphengine::BufferDescriptor *out,
	             unsigned i, unsigned left, unsigned right, void *context, void *tmp) const noexcept override
	{
		auto range = get_row_deps(i);
		const uint8_t *y0 = static_cast<const uint8_t *>(in->get_line(range.first));
		const uint8_t *y1 = static_cast<const uint8_t *>(in->get_line(i));
		const uint8_t *y2 = static_cast<const uint8_t *>(in->get_line(range.second - 1));
		uint8_t *dstp = static_cast<uint8_t *>(out->get_line(i));

		auto s = [](uint8_t x) { return static_cast<int32_t>(x); };

		for (ptrdiff_t j = left; j < static_cast<ptrdiff_t>(right); ++j) {
			ptrdiff_t x0 = std::max(j - 1, static_cast<ptrdiff_t>(0));
			ptrdiff_t x1 = j;
			ptrdiff_t x2 = std::min(j + 1, static_cast<ptrdiff_t>(m_desc.format.width - 1));

			int32_t gx = s(y2[x0]) + 2 * y2[x1] + y2[x2] - y0[x0] - 2 * y0[x1] - y0[x2];
			int32_t gy = s(y0[x2]) + 2 * y1[x2] + y2[x2] - y0[x0] - 2 * y1[x0] - y2[x0];

			float sobel = xsqrtf(static_cast<float>(gx) * gx + static_cast<float>(gy) * gy);
			dstp[j] = static_cast<uint8_t>(xlrintf(std::min(sobel, 255.0f)));
		}
	}
};


class MaskedMergeFilter final : public graphengine::Filter {
	graphengine::FilterDescriptor m_desc = {};
public:
	MaskedMergeFilter(unsigned width, unsigned height)
	{
		m_desc.format.width = width;
		m_desc.format.height = height;
		m_desc.format.bytes_per_sample = 1;

		m_desc.num_deps = 3;
		m_desc.num_planes = 1;
		m_desc.step = 1;

		m_desc.flags.in_place = true;
	}

	const graphengine::FilterDescriptor &descriptor() const noexcept override { return m_desc; }

	std::pair<unsigned, unsigned> get_row_deps(unsigned i) const noexcept override { return{ i, i + 1 }; }

	std::pair<unsigned, unsigned> get_col_deps(unsigned left, unsigned right) const noexcept override { return{ left, right }; }

	void init_context(void *) const noexcept override {}

	void process(const graphengine::BufferDescriptor in[3], const graphengine::BufferDescriptor *out,
	             unsigned i, unsigned left, unsigned right, void *context, void *tmp) const noexcept override
	{
		const uint8_t *src1 = static_cast<const uint8_t *>(in[0].get_line(i));
		const uint8_t *src2 = static_cast<const uint8_t *>(in[1].get_line(i));
		const uint8_t *mask = static_cast<const uint8_t *>(in[2].get_line(i));
		uint8_t *dstp = static_cast<uint8_t *>(out->get_line(i));

		for (unsigned j = left; j < right; ++j) {
			unsigned maskval = mask[j];
			unsigned invmaskval = 255 - maskval;
			unsigned result = maskval * src1[j] + invmaskval * src2[j];
			dstp[j] = static_cast<uint8_t>((result + 127) / 255);
		}
	}
};


class VirtualPadFilter final : public graphengine::Filter {
	graphengine::FilterDescriptor m_desc = {};
	unsigned m_left;
	unsigned m_right;
	unsigned m_top;
	unsigned m_bottom;
public:
	VirtualPadFilter(unsigned src_width, unsigned src_height, unsigned left, unsigned right, unsigned top, unsigned bottom) :
		m_left{ left },
		m_right{ right },
		m_top{ top },
		m_bottom{ bottom }
	{
		if (UINT_MAX - src_width < left || UINT_MAX - src_width - left < right)
			throw std::runtime_error{ "padded dimensions too large" };
		if (UINT_MAX - src_height < top || UINT_MAX - src_height - top < bottom)
			throw std::runtime_error{ "padded dimensions too large" };

		m_desc.format.width = src_width + left + right;
		m_desc.format.height = src_height + top + bottom;
		m_desc.format.bytes_per_sample = 1;

		m_desc.num_deps = 1;
		m_desc.num_planes = 1;
		m_desc.step = 1;
	}

	const graphengine::FilterDescriptor &descriptor() const noexcept override { return m_desc; }

	std::pair<unsigned, unsigned> get_row_deps(unsigned i) const noexcept override
	{
		unsigned src_height = m_desc.format.height - m_top - m_bottom;

		ptrdiff_t srctop = static_cast<ptrdiff_t>(i) - static_cast<ptrdiff_t>(m_top);
		ptrdiff_t srcbot = srctop + 1;
		srctop = std::min(std::max(srctop, static_cast<ptrdiff_t>(0)), static_cast<ptrdiff_t>(src_height));
		srcbot = std::min(std::max(srcbot, static_cast<ptrdiff_t>(0)), static_cast<ptrdiff_t>(src_height));

		return{ static_cast<unsigned>(srctop), static_cast<unsigned>(srcbot) };
	}

	std::pair<unsigned, unsigned> get_col_deps(unsigned left, unsigned right) const noexcept override
	{
		unsigned src_width = m_desc.format.width - m_left - m_right;

		ptrdiff_t srcleft = static_cast<ptrdiff_t>(left) - static_cast<ptrdiff_t>(m_left);
		ptrdiff_t srcright = static_cast<ptrdiff_t>(right) - static_cast<ptrdiff_t>(m_left);
		srcleft = std::min(std::max(srcleft, static_cast<ptrdiff_t>(0)), static_cast<ptrdiff_t>(src_width));
		srcright = std::min(std::max(srcright, static_cast<ptrdiff_t>(0)), static_cast<ptrdiff_t>(src_width));

		return{ static_cast<unsigned>(srcleft), static_cast<unsigned>(srcright) };
	}

	void init_context(void *) const noexcept override {}

	void process(const graphengine::BufferDescriptor *in, const graphengine::BufferDescriptor *out,
	             unsigned i, unsigned left, unsigned right, void *context, void *tmp) const noexcept
	{
		if (i < m_top || i >= m_desc.format.height - m_bottom)
			return;

		const uint8_t *srcp = static_cast<const uint8_t *>(in->get_line(i - m_top));
		uint8_t *dstp = static_cast<uint8_t *>(out->get_line(i));

		auto range = get_col_deps(left, right);
		std::copy_n(srcp + range.first, range.second - range.first, dstp + m_left);
	}
};


class OverlayFilter final : public graphengine::Filter {
	graphengine::FilterDescriptor m_desc = {};
	unsigned m_x0, m_x1, m_y0, m_y1;
public:
	OverlayFilter(unsigned width, unsigned height, unsigned x0, unsigned x1, unsigned y0, unsigned y1) :
		m_x0{ x0 }, m_x1{ x1 }, m_y0{ y0 }, m_y1{ y1 }
	{
		m_desc.format.width = width;
		m_desc.format.height = height;
		m_desc.format.bytes_per_sample = 1;

		m_desc.num_deps = 2;
		m_desc.num_planes = 1;
		m_desc.step = 1;

		m_desc.flags.in_place = true;
	}

	const graphengine::FilterDescriptor &descriptor() const noexcept override { return m_desc; }

	std::pair<unsigned, unsigned> get_row_deps(unsigned i) const noexcept override { return{ i, i + 1 }; }

	std::pair<unsigned, unsigned> get_col_deps(unsigned left, unsigned right) const noexcept override { return{ left, right }; }

	void init_context(void *) const noexcept override {}

	void process(const graphengine::BufferDescriptor in[2], const graphengine::BufferDescriptor *out,
	             unsigned i, unsigned left, unsigned right, void *context, void *tmp) const noexcept
	{
		const uint8_t *srcp0 = static_cast<const uint8_t *>(in[0].get_line(i));
		const uint8_t *srcp1 = static_cast<const uint8_t *>(in[1].get_line(i));
		uint8_t *dstp = static_cast<uint8_t *>(out->get_line(i));

		if (i < m_y0 || i >= m_y1) {
			if (dstp != srcp0)
				std::copy_n(srcp0 + left, right - left, dstp + left);
			return;
		}

		// Optimized path for in-place.
		if (dstp == srcp0) {
			unsigned span_left = std::max(left, m_x0);
			unsigned span_right = std::min(right, m_x1);
			std::copy_n(srcp1 + span_left, span_right - span_left, dstp + span_left);
			return;
		}

		// Left of overlay.
		unsigned span0_left = std::min(left, m_x0);
		unsigned span0_right = std::min(right, m_x0);

		// Overlap.
		unsigned span1_left = std::max(left, m_x0);
		unsigned span1_right = std::min(right, m_x1);

		// Right of overlay.
		unsigned span2_left = std::max(left, m_x1);
		unsigned span2_right = std::max(right, m_x1);

		std::copy_n(srcp0 + span0_left, span0_right - span0_left, dstp + span0_left);
		std::copy_n(srcp1 + span1_left, span1_right - span1_left, dstp + span1_left);
		std::copy_n(srcp0 + span2_left, span2_right - span2_left, dstp + span2_left);
;	}
};

} // namespace


std::unique_ptr<graphengine::Filter> invoke_boxblur(unsigned width, unsigned height)
{
	return std::make_unique<BoxBlurFilter>(width, height);
}

std::unique_ptr<graphengine::Filter> invoke_sobel(unsigned width, unsigned height)
{
	return std::make_unique<SobelFilter>(width, height);
}

std::unique_ptr<graphengine::Filter> invoke_masked_merge(unsigned width, unsigned height)
{
	return std::make_unique<MaskedMergeFilter>(width, height);
}

std::unique_ptr<graphengine::Filter> invoke_virtual_pad(unsigned src_width, unsigned src_height, unsigned left, unsigned right, unsigned top, unsigned bottom)
{
	return std::make_unique<VirtualPadFilter>(src_width, src_height, left, right, top, bottom);
}

std::unique_ptr<graphengine::Filter> invoke_overlay(unsigned width, unsigned height, unsigned x0, unsigned x1, unsigned y0, unsigned y1)
{
	return std::make_unique<OverlayFilter>(width, height, x0, x1, y0, y1);
}
