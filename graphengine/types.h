#pragma once

#ifndef GRAPHENGINE_TYPES_H_
#define GRAPHENGINE_TYPES_H_

#include <utility>

namespace graphengine {

constexpr unsigned BUFFER_MAX = ~0U;

constexpr unsigned FILTER_MAX_DEPS = 3;
constexpr unsigned FILTER_MAX_PLANES = 3;
constexpr unsigned NODE_MAX_PLANES = 4;

constexpr unsigned GRAPH_MAX_ENDPOINTS = 8;

typedef int node_id;
typedef std::pair<node_id, unsigned> node_dep_desc;

class Node;
typedef std::pair<Node *, unsigned> node_dep;

constexpr node_id null_node = -1;
constexpr node_id node_id_max = 1023;


struct PlaneDescriptor {
	unsigned width;
	unsigned height;
	unsigned bytes_per_sample;
};

struct BufferDescriptor {
	void *ptr;
	ptrdiff_t stride;
	unsigned mask;

	void *get_line(unsigned i) const
	{
		return static_cast<uint8_t *>(ptr) + static_cast<ptrdiff_t>(i & mask) * stride;
	}
};

} // namespace graphengine

#endif // GRAPHENGINE_TYPES_H_
