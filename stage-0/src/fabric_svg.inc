/* Faithful C port of rtl_scripts/pigen/fabric.py:render_diagram.  This file is
 * included after the private fabric topology definitions in blocks.c. */

#define SVG_PI 3.14159265358979323846264338327950288
#define SVG_TAU (2.0 * SVG_PI)

typedef struct { double x, y; } svg_point;

typedef struct {
	const char *name;
	int source_count;
	int destination_count;
	double radius;
} svg_unit;

typedef struct {
	fabric_endpoint endpoint;
	int unit;
	int peer;
	int direct;
	double angle;
	double label_x;
	double label_y;
	int label_anchor; /* -1=end, 0=middle, 1=start */
} svg_endpoint;

typedef struct {
	int left;
	int right;
	int kind; /* 0=router, 1=endpoint, 2=direct */
	int left_endpoint;
	int right_endpoint;
	int left_port;
	int right_port;
} svg_edge;

typedef struct {
	const fabric_block *fabric;
	const fabric_topology *topology;
	svg_unit *units;
	size_t unit_count;
	svg_endpoint *endpoints;
	size_t endpoint_count;
	svg_edge *edges;
	size_t edge_count;
	svg_point *positions;
	int node_count;
	int router_count;
	double center_x;
	double center_y;
} svg_layout;

typedef struct { double left, top, right, bottom; } svg_bounds;
typedef struct { double angle; int node; } svg_bearing;

static void svg_append_text(pigen_string *output, const char *text)
{
	for (; *text; text++)
	{
		switch (*text)
		{
			case '&': pigen_append(output, "&amp;"); break;
			case '<': pigen_append(output, "&lt;"); break;
			case '>': pigen_append(output, "&gt;"); break;
			case '\"': pigen_append(output, "&quot;"); break;
			case '\'': pigen_append(output, "&apos;"); break;
			default: pigen_append_range(output, text, 1); break;
		}
	}
}

static const char *svg_endpoint_instance(const fabric_endpoint *endpoint)
{
	return endpoint->direction ? endpoint->source.instance : endpoint->destination.instance;
}

static void svg_append_endpoint_name(pigen_string *output, const fabric_endpoint *endpoint)
{
	if (endpoint->direction)
		pigen_append_format(output, "%s.%s.%s", endpoint->source.instance,
			endpoint->source.port, endpoint->source.handle);
	else
		pigen_append_format(output, "%s.%s", endpoint->destination.instance,
			endpoint->destination.port);
}

static void svg_append_endpoint_data_name(pigen_string *output,
	const fabric_endpoint *endpoint)
{
	pigen_append(output, endpoint->direction ? "source:" : "destination:");
	svg_append_endpoint_name(output, endpoint);
}

static char *svg_port_label(const fabric_endpoint *endpoint)
{
	pigen_string text = {0};
	pigen_append(&text, endpoint->direction ? "out " : "in ");
	if (endpoint->direction)
		pigen_append_format(&text, "%s.%s", endpoint->source.port, endpoint->source.handle);
	else
		pigen_append(&text, endpoint->destination.port);
	return text.data;
}

static void svg_add_endpoint(svg_endpoint **items, size_t *count,
	fabric_endpoint endpoint)
{
	for (size_t index = 0; index < *count; index++)
		if (endpoint_equal(&(*items)[index].endpoint, &endpoint)) return;
	*items = pigen_resize(*items, (*count + 1) * sizeof(**items));
	(*items)[*count] = (svg_endpoint){.endpoint = endpoint, .unit = -1, .peer = -1};
	(*count)++;
}

static int svg_endpoint_compare(const void *left_, const void *right_)
{
	const svg_endpoint *left = left_;
	const svg_endpoint *right = right_;
	return endpoint_compare(&left->endpoint, &right->endpoint);
}

static int svg_unit_compare(const void *left_, const void *right_)
{
	const svg_unit *left = left_;
	const svg_unit *right = right_;
	return strcmp(left->name, right->name);
}

static int svg_connection_pointer_compare(const void *left_, const void *right_)
{
	const fabric_connection *left = *(const fabric_connection *const *)left_;
	const fabric_connection *right = *(const fabric_connection *const *)right_;
	int comparison = strcmp(left->source.instance, right->source.instance);
	if (!comparison) comparison = strcmp(left->source.port, right->source.port);
	if (!comparison) comparison = strcmp(left->source.handle, right->source.handle);
	if (!comparison) comparison = strcmp(left->destination.instance, right->destination.instance);
	if (!comparison) comparison = strcmp(left->destination.port, right->destination.port);
	return comparison;
}

static int svg_find_unit(const svg_layout *layout, const char *name)
{
	for (size_t index = 0; index < layout->unit_count; index++)
		if (!strcmp(layout->units[index].name, name)) return (int)index;
	pigen_fail("internal fabric diagram unit lookup failed");
	return -1;
}

static int svg_find_endpoint(const svg_layout *layout,
	const fabric_endpoint *endpoint)
{
	for (size_t index = 0; index < layout->endpoint_count; index++)
		if (endpoint_equal(&layout->endpoints[index].endpoint, endpoint)) return (int)index;
	pigen_fail("internal fabric diagram endpoint lookup failed");
	return -1;
}

static int svg_unit_node(const svg_layout *layout, int unit)
{
	return layout->router_count + unit;
}

static int svg_node_is_unit(const svg_layout *layout, int node)
{
	return node >= layout->router_count;
}

static int svg_node_unit(const svg_layout *layout, int node)
{
	return node - layout->router_count;
}

static const char *svg_node_name(const svg_layout *layout, int node,
	char router_name[32])
{
	if (svg_node_is_unit(layout, node))
		return layout->units[svg_node_unit(layout, node)].name;
	snprintf(router_name, 32, "r%d", node);
	return router_name;
}

static double svg_node_radius(const svg_layout *layout, int node)
{
	return svg_node_is_unit(layout, node) ?
		layout->units[svg_node_unit(layout, node)].radius : 25.0;
}

static double svg_clearance_radius(const svg_layout *layout, int node)
{
	return svg_node_is_unit(layout, node) ? svg_node_radius(layout, node) + 14.0 : 52.0;
}

static double svg_node_mass(const svg_layout *layout, int node)
{
	return svg_node_is_unit(layout, node) ? 3.2 : 1.0;
}

static void svg_add_edge(svg_layout *layout, svg_edge edge)
{
	layout->edges = pigen_resize(layout->edges,
		(layout->edge_count + 1) * sizeof(*layout->edges));
	layout->edges[layout->edge_count++] = edge;
}

static int svg_router_branch_weight(const svg_layout *layout, int router, int parent)
{
	int weight = 0;
	for (int port = 0; port < 3; port++)
	{
		fabric_attachment attachment = layout->topology->routers[router].ports[port];
		if (attachment.kind == 2) weight++;
		else if (attachment.kind == 1 && attachment.target != parent)
			weight += svg_router_branch_weight(layout, attachment.target, router);
	}
	return weight > 0 ? weight : 1;
}

static int svg_largest_branch(const svg_layout *layout, int router)
{
	int largest = 0;
	for (int port = 0; port < 3; port++)
	{
		fabric_attachment attachment = layout->topology->routers[router].ports[port];
		if (attachment.kind == 1)
		{
			int weight = svg_router_branch_weight(layout, attachment.target, router);
			if (weight > largest) largest = weight;
		}
	}
	return largest;
}

typedef struct { int kind, value, weight; } svg_seed_item;

static int svg_optional_topology_endpoint(const fabric_topology *topology,
	const fabric_endpoint *endpoint);

static void svg_seed_router(svg_layout *layout, int router, int parent,
	double start, double end, int depth, svg_point *targets, int *has_target)
{
	svg_seed_item items[5];
	int item_count = 0;
	int total = 0;
	/* Python sorts endpoints by their textual identity, then child routers by ID. */
	for (size_t endpoint = 0; endpoint < layout->endpoint_count; endpoint++)
	{
		int topology_endpoint = svg_optional_topology_endpoint(layout->topology,
			&layout->endpoints[endpoint].endpoint);
		if (topology_endpoint >= 0 &&
			layout->topology->endpoint_router[topology_endpoint] == router)
			items[item_count++] = (svg_seed_item){2, (int)endpoint, 1};
	}
	for (int child = 0; child < layout->router_count; child++)
		if (child != parent)
			for (int port = 0; port < 3; port++)
			{
				fabric_attachment attachment = layout->topology->routers[router].ports[port];
				if (attachment.kind == 1 && attachment.target == child)
				{
					int weight = svg_router_branch_weight(layout, child, router);
					items[item_count++] = (svg_seed_item){1, child, weight};
					total += weight;
				}
			}
	for (int index = 0; index < item_count; index++) if (items[index].kind == 2) total++;
	if (!total) total = 1;
	double cursor = start;
	for (int index = 0; index < item_count; index++)
	{
		double span = (end - start) * items[index].weight / total;
		double midpoint = cursor + span / 2.0;
		if (items[index].kind == 2)
		{
			targets[items[index].value] = (svg_point){
				layout->center_x + (depth + 1) * 130.0 * cos(midpoint),
				layout->center_y + (depth + 1) * 130.0 * sin(midpoint)};
			has_target[items[index].value] = 1;
		}
		else
			svg_seed_router(layout, items[index].value, router, cursor,
				cursor + span, depth + 1, targets, has_target);
		cursor += span;
	}
}

static void svg_build_model(svg_layout *layout)
{
	const fabric_block *fabric = layout->fabric;
	const fabric_topology *topology = layout->topology;
	const fabric_connection **direct = NULL;
	size_t direct_count = 0;
	for (size_t index = 0; index < fabric->connection_count; index++)
	{
		svg_add_endpoint(&layout->endpoints, &layout->endpoint_count,
			(fabric_endpoint){.direction = 1, .source = fabric->connections[index].source});
		svg_add_endpoint(&layout->endpoints, &layout->endpoint_count,
			(fabric_endpoint){.direction = 0, .destination = fabric->connections[index].destination});
		if (fabric->connections[index].direct)
		{
			direct = pigen_resize(direct, (direct_count + 1) * sizeof(*direct));
			direct[direct_count++] = &fabric->connections[index];
		}
	}
	qsort(layout->endpoints, layout->endpoint_count, sizeof(*layout->endpoints),
		svg_endpoint_compare);
	for (size_t endpoint = 0; endpoint < layout->endpoint_count; endpoint++)
	{
		const char *name = svg_endpoint_instance(&layout->endpoints[endpoint].endpoint);
		int found = -1;
		for (size_t unit = 0; unit < layout->unit_count; unit++)
			if (!strcmp(layout->units[unit].name, name)) found = (int)unit;
		if (found < 0)
		{
			layout->units = pigen_resize(layout->units,
				(layout->unit_count + 1) * sizeof(*layout->units));
			layout->units[layout->unit_count++] = (svg_unit){.name = name};
		}
	}
	qsort(layout->units, layout->unit_count, sizeof(*layout->units), svg_unit_compare);
	for (size_t endpoint = 0; endpoint < layout->endpoint_count; endpoint++)
	{
		int unit = svg_find_unit(layout,
			svg_endpoint_instance(&layout->endpoints[endpoint].endpoint));
		layout->endpoints[endpoint].unit = unit;
		if (layout->endpoints[endpoint].endpoint.direction)
			layout->units[unit].source_count++;
		else layout->units[unit].destination_count++;
	}
	for (size_t unit = 0; unit < layout->unit_count; unit++)
	{
		int ports = layout->units[unit].source_count + layout->units[unit].destination_count;
		layout->units[unit].radius = 38.0 + 5.0 * ports;
		if (layout->units[unit].radius < 58.0) layout->units[unit].radius = 58.0;
	}
	layout->router_count = (int)topology->router_count;
	layout->node_count = layout->router_count + (int)layout->unit_count;
	layout->positions = pigen_resize(NULL,
		(size_t)layout->node_count * sizeof(*layout->positions));

	for (int router = 0; router < layout->router_count; router++)
		for (int port = 0; port < 3; port++)
		{
			fabric_attachment attachment = topology->routers[router].ports[port];
			if (attachment.kind == 1 && router < attachment.target)
				svg_add_edge(layout, (svg_edge){router, attachment.target, 0,
					-1, -1, port, attachment.port});
			else if (attachment.kind == 2)
			{
				fabric_endpoint endpoint = topology->endpoints[attachment.target];
				int diagram_endpoint = svg_find_endpoint(layout, &endpoint);
				int unit_node = svg_unit_node(layout,
					layout->endpoints[diagram_endpoint].unit);
				layout->endpoints[diagram_endpoint].peer = router;
				if (endpoint.direction)
					svg_add_edge(layout, (svg_edge){unit_node, router, 1,
						diagram_endpoint, -1, -1, port});
				else
					svg_add_edge(layout, (svg_edge){router, unit_node, 1,
						-1, diagram_endpoint, port, -1});
			}
		}
	qsort(direct, direct_count, sizeof(*direct), svg_connection_pointer_compare);
	for (size_t index = 0; index < direct_count; index++)
	{
		fabric_endpoint source = {.direction = 1, .source = direct[index]->source};
		fabric_endpoint destination = {.direction = 0, .destination = direct[index]->destination};
		int source_index = svg_find_endpoint(layout, &source);
		int destination_index_ = svg_find_endpoint(layout, &destination);
		int source_node = svg_unit_node(layout, layout->endpoints[source_index].unit);
		int destination_node = svg_unit_node(layout, layout->endpoints[destination_index_].unit);
		svg_add_edge(layout, (svg_edge){source_node, destination_node, 2,
			source_index, destination_index_, -1, -1});
		layout->endpoints[source_index].peer = destination_node;
		layout->endpoints[destination_index_].peer = source_node;
		layout->endpoints[source_index].direct = 1;
		layout->endpoints[destination_index_].direct = 1;
	}
	free(direct);
}

static int svg_optional_topology_endpoint(const fabric_topology *topology,
	const fabric_endpoint *endpoint)
{
	for (size_t index = 0; index < topology->endpoint_count; index++)
		if (endpoint_equal(&topology->endpoints[index], endpoint)) return (int)index;
	return -1;
}

static int svg_unit_order_before(const svg_layout *layout, const svg_point *home,
	int left, int right)
{
	double left_angle = atan2(home[svg_unit_node(layout, left)].y - layout->center_y,
		home[svg_unit_node(layout, left)].x - layout->center_x);
	double right_angle = atan2(home[svg_unit_node(layout, right)].y - layout->center_y,
		home[svg_unit_node(layout, right)].x - layout->center_x);
	if (left_angle != right_angle) return left_angle < right_angle;
	return strcmp(layout->units[left].name, layout->units[right].name) < 0;
}

static void svg_seed_initial_positions(svg_layout *layout)
{
	int scale_count = (int)layout->unit_count;
	if (layout->router_count > scale_count) scale_count = layout->router_count;
	if (scale_count < 1) scale_count = 1;
	double network_height = 260.0 + scale_count * 100.0;
	if (network_height < 900.0) network_height = 900.0;
	layout->center_x = 800.0;
	layout->center_y = network_height / 2.0;
	svg_point *targets = pigen_resize(NULL,
		layout->endpoint_count * sizeof(*targets));
	int *has_target = pigen_resize(NULL,
		layout->endpoint_count * sizeof(*has_target));
	svg_point *home = pigen_resize(NULL,
		(size_t)layout->node_count * sizeof(*home));
	memset(targets, 0, layout->endpoint_count * sizeof(*targets));
	memset(has_target, 0, layout->endpoint_count * sizeof(*has_target));
	memset(home, 0, (size_t)layout->node_count * sizeof(*home));

	if (layout->router_count)
	{
		int root = 0;
		int best = svg_largest_branch(layout, 0);
		for (int router = 1; router < layout->router_count; router++)
		{
			int candidate = svg_largest_branch(layout, router);
			if (candidate < best) { root = router; best = candidate; }
		}
		svg_seed_router(layout, root, -1, 0.0, SVG_TAU, 0, targets, has_target);
	}
	for (size_t unit = 0; unit < layout->unit_count; unit++)
	{
		double x = 0.0, y = 0.0;
		int count = 0;
		for (size_t endpoint = 0; endpoint < layout->endpoint_count; endpoint++)
			if (layout->endpoints[endpoint].unit == (int)unit && has_target[endpoint])
			{
				x += targets[endpoint].x; y += targets[endpoint].y; count++;
			}
		if (count)
			home[svg_unit_node(layout, (int)unit)] =
				(svg_point){x / count, y / count};
		else
			home[svg_unit_node(layout, (int)unit)] = (svg_point){
				260.0 + (unit % 4) * 355.0, 110.0 + (unit / 4) * 175.0};
	}
	int *ordered = pigen_resize(NULL, layout->unit_count * sizeof(*ordered));
	for (size_t unit = 0; unit < layout->unit_count; unit++)
	{
		size_t at = unit;
		while (at && svg_unit_order_before(layout, home, (int)unit, ordered[at - 1]))
		{
			ordered[at] = ordered[at - 1]; at--;
		}
		ordered[at] = (int)unit;
	}
	double initial_radius = (1600.0 < network_height ? 1600.0 : network_height) * 0.36;
	for (size_t index = 0; index < layout->unit_count; index++)
	{
		double angle = SVG_TAU * index / layout->unit_count;
		home[svg_unit_node(layout, ordered[index])] = (svg_point){
			layout->center_x + initial_radius * cos(angle),
			layout->center_y + initial_radius * sin(angle)};
	}
	for (int router = 0; router < layout->router_count; router++)
		home[router] = (svg_point){layout->center_x, layout->center_y};
	memcpy(layout->positions, home, (size_t)layout->node_count * sizeof(*home));
	free(ordered); free(home); free(has_target); free(targets);
}

static int svg_node_name_compare(const svg_layout *layout, int left, int right)
{
	char left_router[32], right_router[32];
	return strcmp(svg_node_name(layout, left, left_router),
		svg_node_name(layout, right, right_router));
}

static void svg_sort_bearings(const svg_layout *layout, svg_bearing *items,
	int count)
{
	for (int index = 1; index < count; index++)
	{
		svg_bearing item = items[index];
		int at = index;
		while (at > 0 && (item.angle < items[at - 1].angle ||
			(item.angle == items[at - 1].angle &&
				svg_node_name_compare(layout, item.node, items[at - 1].node) < 0)))
		{
			items[at] = items[at - 1]; at--;
		}
		items[at] = item;
	}
}

static double svg_orientation_points(svg_point a, svg_point b, svg_point c)
{
	return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

static int svg_proper_segments_cross(svg_point a, svg_point b,
	svg_point c, svg_point d)
{
	return svg_orientation_points(a, b, c) * svg_orientation_points(a, b, d) < -0.001 &&
		svg_orientation_points(c, d, a) * svg_orientation_points(c, d, b) < -0.001;
}

static void svg_resolve_collisions(svg_layout *layout, int rounds,
	int stop_when_clear)
{
	for (int round = 0; round < rounds; round++)
	{
		int adjusted = 0;
		for (int left = 0; left < layout->node_count; left++)
		{
			double lx = layout->positions[left].x;
			double ly = layout->positions[left].y;
			for (int right = left + 1; right < layout->node_count; right++)
			{
				double rx = layout->positions[right].x;
				double ry = layout->positions[right].y;
				double dx = rx - lx, dy = ry - ly;
				double distance = hypot(dx, dy);
				double minimum = svg_clearance_radius(layout, left) +
					svg_clearance_radius(layout, right);
				if (distance >= minimum) continue;
				adjusted = 1;
				if (distance < 0.001)
				{
					double angle = (left + 1) * 2.399963229728653;
					dx = cos(angle); dy = sin(angle); distance = 1.0;
				}
				double correction = minimum - distance;
				double left_inverse = 1.0 / svg_node_mass(layout, left);
				double right_inverse = 1.0 / svg_node_mass(layout, right);
				double left_share = left_inverse / (left_inverse + right_inverse);
				double right_share = 1.0 - left_share;
				double move_x = dx / distance * correction;
				double move_y = dy / distance * correction;
				layout->positions[left] = (svg_point){
					lx - move_x * left_share, ly - move_y * left_share};
				layout->positions[right] = (svg_point){
					rx + move_x * right_share, ry + move_y * right_share};
			}
		}
		if (stop_when_clear && !adjusted) break;
	}
}

static void svg_relax_layout(svg_layout *layout)
{
	svg_point *forces = pigen_resize(NULL,
		(size_t)layout->node_count * sizeof(*forces));
	svg_bearing *bearings = pigen_resize(NULL,
		(layout->edge_count ? layout->edge_count : 1) * sizeof(*bearings));
	for (int iteration = 0; iteration < 6000; iteration++)
	{
		memset(forces, 0, (size_t)layout->node_count * sizeof(*forces));
		for (size_t edge_index = 0; edge_index < layout->edge_count; edge_index++)
		{
			svg_edge edge = layout->edges[edge_index];
			svg_point left = layout->positions[edge.left];
			svg_point right = layout->positions[edge.right];
			double dx = right.x - left.x, dy = right.y - left.y;
			double distance = hypot(dx, dy);
			if (distance < 1.0) distance = 1.0;
			double visible_length = edge.kind == 2 ? 220.0 : 190.0;
			double rest_length = visible_length + svg_node_radius(layout, edge.left) +
				svg_node_radius(layout, edge.right);
			double strength = 0.320 * (distance - rest_length);
			double fx = dx / distance * strength, fy = dy / distance * strength;
			forces[edge.left].x += fx; forces[edge.left].y += fy;
			forces[edge.right].x -= fx; forces[edge.right].y -= fy;
			double snapped = round(atan2(dy, dx) / (SVG_PI / 4.0)) * (SVG_PI / 4.0);
			double normal_x = -sin(snapped), normal_y = cos(snapped);
			double offset = dx * normal_x + dy * normal_y;
			forces[edge.left].x += normal_x * offset * 0.035;
			forces[edge.left].y += normal_y * offset * 0.035;
			forces[edge.right].x -= normal_x * offset * 0.035;
			forces[edge.right].y -= normal_y * offset * 0.035;
		}
		for (int router = 0; router < layout->router_count; router++)
		{
			int count = 0;
			for (size_t edge_index = 0; edge_index < layout->edge_count; edge_index++)
			{
				svg_edge edge = layout->edges[edge_index];
				if (edge.kind == 2) continue;
				int neighbour = edge.left == router ? edge.right :
					(edge.right == router ? edge.left : -1);
				if (neighbour >= 0)
					bearings[count++] = (svg_bearing){
						atan2(layout->positions[neighbour].y - layout->positions[router].y,
							layout->positions[neighbour].x - layout->positions[router].x),
						neighbour};
			}
			if (count < 2) continue;
			svg_sort_bearings(layout, bearings, count);
			double target_gap = SVG_TAU / count;
			for (int index = 0; index < count; index++)
			{
				svg_bearing first = bearings[index];
				svg_bearing next = bearings[(index + 1) % count];
				double gap = fmod(next.angle - first.angle + SVG_TAU, SVG_TAU);
				double first_distance = hypot(
					layout->positions[first.node].x - layout->positions[router].x,
					layout->positions[first.node].y - layout->positions[router].y);
				double next_distance = hypot(
					layout->positions[next.node].x - layout->positions[router].x,
					layout->positions[next.node].y - layout->positions[router].y);
				double magnitude = (gap - target_gap) *
					(first_distance < next_distance ? first_distance : next_distance) * 0.055;
				forces[first.node].x += -sin(first.angle) * magnitude;
				forces[first.node].y += cos(first.angle) * magnitude;
				forces[next.node].x -= -sin(next.angle) * magnitude;
				forces[next.node].y -= cos(next.angle) * magnitude;
			}
		}
		for (size_t direct_index = 0; direct_index < layout->edge_count; direct_index++)
		{
			svg_edge direct = layout->edges[direct_index];
			if (direct.kind != 2) continue;
			svg_point a = layout->positions[direct.left];
			svg_point b = layout->positions[direct.right];
			double direct_dx = b.x - a.x, direct_dy = b.y - a.y;
			double direct_length = hypot(direct_dx, direct_dy);
			if (direct_length < 1.0) direct_length = 1.0;
			for (size_t other_index = 0; other_index < layout->edge_count; other_index++)
			{
				svg_edge other = layout->edges[other_index];
				if (other.kind == 2 || direct.left == other.left || direct.left == other.right ||
					direct.right == other.left || direct.right == other.right) continue;
				svg_point c = layout->positions[other.left];
				svg_point d = layout->positions[other.right];
				if (!svg_proper_segments_cross(a, b, c, d)) continue;
				double midpoint_side = direct_dx * ((c.y + d.y) / 2.0 - a.y) -
					direct_dy * ((c.x + d.x) / 2.0 - a.x);
				double side = midpoint_side >= 0.0 ? 1.0 : -1.0;
				double push_x = -direct_dy / direct_length * side * 1.8;
				double push_y = direct_dx / direct_length * side * 1.8;
				forces[other.left].x += push_x; forces[other.left].y += push_y;
				forces[other.right].x += push_x; forces[other.right].y += push_y;
			}
		}
		for (int node = 0; node < layout->node_count; node++)
		{
			if (svg_node_is_unit(layout, node))
			{
				double radial_x = layout->positions[node].x - layout->center_x;
				double radial_y = layout->positions[node].y - layout->center_y;
				double radial_length = hypot(radial_x, radial_y);
				if (radial_length > 0.001)
				{
					forces[node].x += radial_x / radial_length * 0.18;
					forces[node].y += radial_y / radial_length * 0.18;
				}
			}
			double mass = svg_node_mass(layout, node);
			layout->positions[node].x += forces[node].x / mass;
			layout->positions[node].y += forces[node].y / mass;
		}
		if (iteration >= 1200) svg_resolve_collisions(layout, 3, 0);
	}
	svg_resolve_collisions(layout, 600, 1);
	free(bearings); free(forces);
}

static void svg_pack_components(svg_layout *layout)
{
	size_t matrix_size = (size_t)layout->node_count * (size_t)layout->node_count;
	unsigned char *adjacency = pigen_resize(NULL, matrix_size ? matrix_size : 1);
	unsigned char *unseen = pigen_resize(NULL,
		layout->node_count ? (size_t)layout->node_count : 1);
	int *component = pigen_resize(NULL,
		(size_t)layout->node_count * sizeof(*component));
	int *pending = pigen_resize(NULL,
		(size_t)layout->node_count * sizeof(*pending));
	memset(adjacency, 0, matrix_size);
	memset(unseen, 1, (size_t)layout->node_count);
	for (size_t edge = 0; edge < layout->edge_count; edge++)
	{
		int left = layout->edges[edge].left, right = layout->edges[edge].right;
		adjacency[(size_t)left * layout->node_count + right] = 1;
		adjacency[(size_t)right * layout->node_count + left] = 1;
	}
	int component_count = 0;
	for (;;)
	{
		int root = -1;
		for (int node = 0; node < layout->node_count; node++) if (unseen[node] &&
			(root < 0 || svg_node_name_compare(layout, node, root) < 0)) root = node;
		if (root < 0) break;
		int head = 0, tail = 0;
		pending[tail++] = root; unseen[root] = 0;
		while (head < tail)
		{
			int node = pending[head++];
			component[node] = component_count;
			for (int neighbour = 0; neighbour < layout->node_count; neighbour++)
				if (unseen[neighbour] &&
					adjacency[(size_t)node * layout->node_count + neighbour])
				{
					unseen[neighbour] = 0; pending[tail++] = neighbour;
				}
		}
		component_count++;
	}
	if (component_count > 1)
	{
		double *minimum_x = pigen_resize(NULL, (size_t)component_count * sizeof(*minimum_x));
		double *maximum_x = pigen_resize(NULL, (size_t)component_count * sizeof(*maximum_x));
		double *minimum_y = pigen_resize(NULL, (size_t)component_count * sizeof(*minimum_y));
		double *maximum_y = pigen_resize(NULL, (size_t)component_count * sizeof(*maximum_y));
		for (int index = 0; index < component_count; index++)
		{
			minimum_x[index] = minimum_y[index] = 1e300;
			maximum_x[index] = maximum_y[index] = -1e300;
		}
		for (int node = 0; node < layout->node_count; node++)
		{
			int id = component[node]; double radius = svg_clearance_radius(layout, node);
			if (layout->positions[node].x - radius < minimum_x[id]) minimum_x[id] = layout->positions[node].x - radius;
			if (layout->positions[node].x + radius > maximum_x[id]) maximum_x[id] = layout->positions[node].x + radius;
			if (layout->positions[node].y - radius < minimum_y[id]) minimum_y[id] = layout->positions[node].y - radius;
			if (layout->positions[node].y + radius > maximum_y[id]) maximum_y[id] = layout->positions[node].y + radius;
		}
		double cell_width = 0.0, cell_height = 0.0;
		for (int index = 0; index < component_count; index++)
		{
			double width = maximum_x[index] - minimum_x[index];
			double height = maximum_y[index] - minimum_y[index];
			if (width > cell_width) cell_width = width;
			if (height > cell_height) cell_height = height;
		}
		int columns = (int)ceil(sqrt(component_count));
		for (int id = 0; id < component_count; id++)
		{
			int row = id / columns, column = id % columns;
			double shift_x = column * (cell_width + 220.0) - minimum_x[id];
			double shift_y = row * (cell_height + 220.0) - minimum_y[id];
			for (int node = 0; node < layout->node_count; node++) if (component[node] == id)
			{
				layout->positions[node].x += shift_x;
				layout->positions[node].y += shift_y;
			}
		}
		free(maximum_y); free(minimum_y); free(maximum_x); free(minimum_x);
	}
	free(pending); free(component); free(unseen); free(adjacency);
}

static int svg_edges_share_node(const svg_edge *left, const svg_edge *right)
{
	return left->left == right->left || left->left == right->right ||
		left->right == right->left || left->right == right->right;
}

static int svg_edges_cross(const svg_layout *layout,
	const svg_edge *first, const svg_edge *second)
{
	if (svg_edges_share_node(first, second)) return 0;
	return svg_proper_segments_cross(layout->positions[first->left],
		layout->positions[first->right], layout->positions[second->left],
		layout->positions[second->right]);
}

static int svg_crossing_count(const svg_layout *layout)
{
	int count = 0;
	for (size_t first = 0; first < layout->edge_count; first++)
		for (size_t second = first + 1; second < layout->edge_count; second++)
			count += svg_edges_cross(layout, &layout->edges[first], &layout->edges[second]);
	return count;
}

static int svg_crossing_cost(const svg_layout *layout)
{
	int cost = 0;
	for (size_t first = 0; first < layout->edge_count; first++)
		for (size_t second = first + 1; second < layout->edge_count; second++)
			if (svg_edges_cross(layout, &layout->edges[first], &layout->edges[second]))
				cost += layout->edges[first].kind == 2 || layout->edges[second].kind == 2 ? 8 : 1;
	return cost;
}

static int svg_direct_crossing_count(const svg_layout *layout)
{
	int count = 0;
	for (size_t first = 0; first < layout->edge_count; first++)
		for (size_t second = first + 1; second < layout->edge_count; second++)
			if ((layout->edges[first].kind == 2 || layout->edges[second].kind == 2) &&
				svg_edges_cross(layout, &layout->edges[first], &layout->edges[second])) count++;
	return count;
}

static int svg_clear_of_nodes(const svg_layout *layout, int node, double x, double y)
{
	for (int other = 0; other < layout->node_count; other++) if (other != node &&
		hypot(x - layout->positions[other].x, y - layout->positions[other].y) <
			svg_clearance_radius(layout, node) + svg_clearance_radius(layout, other)) return 0;
	return 1;
}

static int svg_minimize_crossings(svg_layout *layout)
{
	static const int offsets[][2] = {
		{-126,0},{126,0},{0,-126},{0,126},{-90,-90},{90,-90},{-90,90},{90,90},
		{-84,0},{84,0},{0,-84},{0,84},{-60,-60},{60,-60},{-60,60},{60,60},
		{-42,0},{42,0},{0,-42},{0,42},{-30,-30},{30,-30},{-30,30},{30,30}
	};
	int before = svg_crossing_count(layout);
	for (int pass = 0; pass < 48; pass++)
	{
		int improved = 0;
		for (int offset = 0; offset < layout->node_count; offset++)
		{
			int node = (offset + pass) % layout->node_count;
			svg_point original = layout->positions[node];
			svg_point best_position = original;
			int best = svg_crossing_cost(layout);
			int best_direct = svg_direct_crossing_count(layout);
			for (size_t candidate_index = 0;
				candidate_index < sizeof(offsets) / sizeof(offsets[0]); candidate_index++)
			{
				svg_point candidate = {original.x + offsets[candidate_index][0],
					original.y + offsets[candidate_index][1]};
				if (!svg_clear_of_nodes(layout, node, candidate.x, candidate.y)) continue;
				layout->positions[node] = candidate;
				int candidate_cost = svg_crossing_cost(layout);
				int candidate_direct = svg_direct_crossing_count(layout);
				if (candidate_direct < best_direct ||
					(candidate_direct == best_direct && candidate_cost < best))
				{
					best = candidate_cost; best_direct = candidate_direct;
					best_position = candidate;
				}
			}
			layout->positions[node] = best_position;
			if (best_position.x != original.x || best_position.y != original.y) improved = 1;
		}
		if (!improved) break;
	}
	return before;
}

static void svg_fit_viewport(svg_layout *layout, int *width, int *network_height)
{
	double minimum_x = 1e300, maximum_x = -1e300;
	double minimum_y = 1e300, maximum_y = -1e300;
	for (int node = 0; node < layout->node_count; node++)
	{
		double radius = svg_clearance_radius(layout, node);
		if (layout->positions[node].x - radius < minimum_x) minimum_x = layout->positions[node].x - radius;
		if (layout->positions[node].x + radius > maximum_x) maximum_x = layout->positions[node].x + radius;
		if (layout->positions[node].y - radius < minimum_y) minimum_y = layout->positions[node].y - radius;
		if (layout->positions[node].y + radius > maximum_y) maximum_y = layout->positions[node].y + radius;
	}
	double shift_x = 125.0 - minimum_x, shift_y = 125.0 - minimum_y;
	for (int node = 0; node < layout->node_count; node++)
	{
		layout->positions[node].x += shift_x;
		layout->positions[node].y += shift_y;
	}
	*width = (int)ceil(maximum_x - minimum_x + 250.0);
	*network_height = (int)ceil(maximum_y - minimum_y + 250.0);
}

static double svg_endpoint_bearing(const svg_layout *layout, int endpoint)
{
	int unit_node = svg_unit_node(layout, layout->endpoints[endpoint].unit);
	int peer = layout->endpoints[endpoint].peer;
	return atan2(layout->positions[peer].y - layout->positions[unit_node].y,
		layout->positions[peer].x - layout->positions[unit_node].x);
}

static int svg_endpoint_text_compare(const svg_endpoint *left,
	const svg_endpoint *right)
{
	return endpoint_compare(&left->endpoint, &right->endpoint);
}

static void svg_sort_endpoint_indices(const svg_layout *layout, int *items,
	int count, int by_port_angle)
{
	for (int index = 1; index < count; index++)
	{
		int item = items[index], at = index;
		while (at > 0)
		{
			int previous = items[at - 1];
			double item_angle = by_port_angle ? layout->endpoints[item].angle :
				svg_endpoint_bearing(layout, item);
			double previous_angle = by_port_angle ? layout->endpoints[previous].angle :
				svg_endpoint_bearing(layout, previous);
			if (item_angle > previous_angle || (item_angle == previous_angle &&
				svg_endpoint_text_compare(&layout->endpoints[item],
					&layout->endpoints[previous]) >= 0)) break;
			items[at] = previous; at--;
		}
		items[at] = item;
	}
}

static svg_point svg_port_position(const svg_layout *layout, int endpoint)
{
	const svg_endpoint *port = &layout->endpoints[endpoint];
	int node = svg_unit_node(layout, port->unit);
	double radius = layout->units[port->unit].radius;
	return (svg_point){layout->positions[node].x + radius * cos(port->angle),
		layout->positions[node].y + radius * sin(port->angle)};
}

static svg_bounds svg_label_bounds(double x, double y, const char *text,
	int anchor)
{
	double width = strlen(text) * 6.1;
	double left = anchor == 0 ? x - width / 2.0 : anchor < 0 ? x - width : x;
	return (svg_bounds){left, y - 11.0, left + width, y + 3.0};
}

static int svg_bounds_overlap(svg_bounds left, svg_bounds right)
{
	return left.left < right.right && right.left < left.right &&
		left.top < right.bottom && right.top < left.bottom;
}

static void svg_place_ports_and_labels(svg_layout *layout)
{
	int *ports = pigen_resize(NULL,
		(layout->endpoint_count ? layout->endpoint_count : 1) * sizeof(*ports));
	for (size_t endpoint = 0; endpoint < layout->endpoint_count; endpoint++)
		if (layout->endpoints[endpoint].direct)
			layout->endpoints[endpoint].angle = svg_endpoint_bearing(layout, (int)endpoint);
	for (size_t unit = 0; unit < layout->unit_count; unit++)
		for (int octant = 0; octant < 8; octant++)
		{
			int count = 0;
			for (size_t endpoint = 0; endpoint < layout->endpoint_count; endpoint++)
				if (layout->endpoints[endpoint].unit == (int)unit &&
					!layout->endpoints[endpoint].direct)
				{
					double bearing = svg_endpoint_bearing(layout, (int)endpoint);
					int endpoint_octant = (int)nearbyint(bearing / (SVG_PI / 4.0));
					endpoint_octant %= 8; if (endpoint_octant < 0) endpoint_octant += 8;
					if (endpoint_octant == octant) ports[count++] = (int)endpoint;
				}
			if (!count) continue;
			svg_sort_endpoint_indices(layout, ports, count, 0);
			double direction_x = 0.0, direction_y = 0.0;
			for (int index = 0; index < count; index++)
			{
				double bearing = svg_endpoint_bearing(layout, ports[index]);
				direction_x += cos(bearing); direction_y += sin(bearing);
			}
			double base = atan2(direction_y, direction_x);
			for (int index = 0; index < count; index++)
				layout->endpoints[ports[index]].angle = base +
					(index - (count - 1) / 2.0) * 0.13;
		}

	for (size_t unit = 0; unit < layout->unit_count; unit++)
	{
		int count = 0;
		for (size_t endpoint = 0; endpoint < layout->endpoint_count; endpoint++)
			if (layout->endpoints[endpoint].unit == (int)unit) ports[count++] = (int)endpoint;
		svg_sort_endpoint_indices(layout, ports, count, 1);
		svg_bounds *occupied = pigen_resize(NULL,
			(count ? (size_t)count : 1) * sizeof(*occupied));
		int occupied_count = 0;
		int unit_node = svg_unit_node(layout, (int)unit);
		for (int index = 0; index < count; index++)
		{
			int endpoint = ports[index];
			double angle = layout->endpoints[endpoint].angle;
			char *text = svg_port_label(&layout->endpoints[endpoint].endpoint);
			int anchor = cos(angle) > 0.25 ? 1 : cos(angle) < -0.25 ? -1 : 0;
			double label_x = 0.0, label_y = 0.0;
			svg_bounds bounds = {0};
			for (int extra = 14; extra < 115; extra += 10)
			{
				label_x = layout->positions[unit_node].x +
					(layout->units[unit].radius + extra) * cos(angle);
				label_y = layout->positions[unit_node].y +
					(layout->units[unit].radius + extra) * sin(angle) + 4.0;
				bounds = svg_label_bounds(label_x, label_y, text, anchor);
				int collision = 0;
				for (int other = 0; other < occupied_count; other++)
					if (svg_bounds_overlap(bounds, occupied[other])) collision = 1;
				if (!collision) break;
			}
			occupied[occupied_count++] = bounds;
			layout->endpoints[endpoint].label_x = label_x;
			layout->endpoints[endpoint].label_y = label_y;
			layout->endpoints[endpoint].label_anchor = anchor;
			free(text);
		}
		free(occupied);
	}
	/* The endpoint array is already sorted by endpoint text. */
	for (int pass = 0; pass < 8; pass++)
	{
		int moved = 0;
		for (size_t endpoint = 0; endpoint < layout->endpoint_count; endpoint++)
		{
			char *text = svg_port_label(&layout->endpoints[endpoint].endpoint);
			svg_bounds bounds = svg_label_bounds(layout->endpoints[endpoint].label_x,
				layout->endpoints[endpoint].label_y, text,
				layout->endpoints[endpoint].label_anchor);
			for (;;)
			{
				int collision = 0;
				for (size_t other = 0; other < layout->endpoint_count; other++) if (other != endpoint)
				{
					char *other_text = svg_port_label(&layout->endpoints[other].endpoint);
					svg_bounds other_bounds = svg_label_bounds(
						layout->endpoints[other].label_x, layout->endpoints[other].label_y,
						other_text, layout->endpoints[other].label_anchor);
					free(other_text);
					if (svg_bounds_overlap(bounds, other_bounds)) { collision = 1; break; }
				}
				if (!collision) break;
				layout->endpoints[endpoint].label_x += 12.0 * cos(layout->endpoints[endpoint].angle);
				layout->endpoints[endpoint].label_y += 12.0 * sin(layout->endpoints[endpoint].angle);
				bounds = svg_label_bounds(layout->endpoints[endpoint].label_x,
					layout->endpoints[endpoint].label_y, text,
					layout->endpoints[endpoint].label_anchor);
				moved = 1;
			}
			free(text);
		}
		if (!moved) break;
	}
	free(ports);
}

static svg_point svg_edge_position(const svg_layout *layout, int node,
	int endpoint, int other)
{
	if (svg_node_is_unit(layout, node)) return svg_port_position(layout, endpoint);
	svg_point position = layout->positions[node], peer = layout->positions[other];
	double distance = hypot(peer.x - position.x, peer.y - position.y);
	if (distance < 1.0) distance = 1.0;
	return (svg_point){position.x + 25.0 * (peer.x - position.x) / distance,
		position.y + 25.0 * (peer.y - position.y) / distance};
}

static void svg_wire_points(const svg_layout *layout, const svg_edge *edge,
	svg_point *left, svg_point *right)
{
	*left = svg_edge_position(layout, edge->left, edge->left_endpoint, edge->right);
	*right = svg_edge_position(layout, edge->right, edge->right_endpoint, edge->left);
}

static int svg_rendered_edges_cross(const svg_layout *layout,
	const svg_edge *first, const svg_edge *second)
{
	if (svg_edges_share_node(first, second)) return 0;
	svg_point first_left, first_right, second_left, second_right;
	svg_wire_points(layout, first, &first_left, &first_right);
	svg_wire_points(layout, second, &second_left, &second_right);
	return svg_proper_segments_cross(first_left, first_right, second_left, second_right);
}

static int svg_rendered_direct_crossings(const svg_layout *layout)
{
	int count = 0;
	for (size_t first = 0; first < layout->edge_count; first++)
		for (size_t second = first + 1; second < layout->edge_count; second++)
			if ((layout->edges[first].kind == 2 || layout->edges[second].kind == 2) &&
				svg_rendered_edges_cross(layout, &layout->edges[first],
					&layout->edges[second])) count++;
	return count;
}

static const char *svg_edge_kind_name(int kind)
{
	return kind == 0 ? "router-link" : kind == 1 ? "endpoint-link" : "direct-link";
}

static void svg_append_node_name(pigen_string *output, const svg_layout *layout,
	int node)
{
	if (svg_node_is_unit(layout, node))
		svg_append_text(output, layout->units[svg_node_unit(layout, node)].name);
	else pigen_append_format(output, "r%d", node);
}

static void svg_append_edge_label(pigen_string *output, const svg_edge *edge)
{
	if (edge->kind == 0)
		pigen_append_format(output, "r%d.p%d ↔ r%d.p%d", edge->left,
			edge->left_port, edge->right, edge->right_port);
	else if (edge->kind == 1)
	{
		int router = edge->left_endpoint >= 0 ? edge->right : edge->left;
		int port = edge->left_endpoint >= 0 ? edge->right_port : edge->left_port;
		pigen_append_format(output, "r%d.p%d", router, port);
	}
	else pigen_append(output, "direct &gt;");
}

static int svg_direct_has_reverse(const svg_layout *layout, const svg_edge *edge)
{
	for (size_t index = 0; index < layout->edge_count; index++)
		if (layout->edges[index].kind == 2 && layout->edges[index].left == edge->right &&
			layout->edges[index].right == edge->left) return 1;
	return 0;
}

static void svg_append_connection_arrow(pigen_string *output,
	const fabric_connection *connection)
{
	if (connection->direct) pigen_append(output, "&gt;");
	else
	{
		for (int dash = 0; dash < connection->tier; dash++) pigen_append(output, "-");
		pigen_append(output, "&gt;");
	}
}

static void svg_append_binary_path(pigen_string *output, unsigned long long value,
	int width)
{
	for (int bit = width - 1; bit >= 0; bit--)
		pigen_append(output, (value >> bit) & 1ULL ? "1" : "0");
}

static void svg_render_output(pigen_string *output, svg_layout *layout,
	int width, int network_height, int crossings_before, int crossings)
{
	int direct_crossings = svg_rendered_direct_crossings(layout);
	int legend_y = network_height + 45;
	int diagram_height = legend_y + 32 + (int)layout->fabric->connection_count * 20;
	pigen_append(output, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
	pigen_append_format(output,
		"<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%d\" height=\"%d\" viewBox=\"0 0 %d %d\" role=\"img\" aria-labelledby=\"title desc\" data-layout=\"tree-spring\" data-crossings-before=\"%d\" data-crossings=\"%d\" data-direct-crossings=\"%d\">\n",
		width, diagram_height, width, diagram_height, crossings_before, crossings,
		direct_crossings);
	pigen_append(output, "  <title id=\"title\">Pigen routing network: ");
	svg_append_text(output, layout->fabric->name); pigen_append(output, "</title>\n");
	pigen_append(output,
		"  <desc id=\"desc\">Elaborated source-routed fabric topology, seeded from its router tree and refined with angular snap springs, unit out-pull, and padded collision resolution. Blue links are hard direct connections.</desc>\n"
		"  <defs><marker id=\"arrow\" viewBox=\"0 0 10 10\" refX=\"9\" refY=\"5\" markerWidth=\"6\" markerHeight=\"6\" orient=\"auto\"><path d=\"M 0 0 L 10 5 L 0 10 z\" fill=\"#52606d\"/></marker></defs>\n"
		"  <style>text{font-family:ui-monospace,monospace;font-size:13px;fill:#172033}.edge{stroke:#52606d;stroke-width:2.2;fill:none}.direct{stroke:#1479c9;stroke-width:3.4}.port{font-size:11px;fill:#52606d;paint-order:stroke;stroke:#fff;stroke-width:3px}.router{fill:#fff4d6;stroke:#a05a00;stroke-width:2}.unit{fill:#dbeafe;stroke:#1d4ed8;stroke-width:2.5}.label{font-size:12px}.legend{font-size:12px}</style>\n"
		"  <text x=\"24\" y=\"30\" font-size=\"18\">fabric ");
	svg_append_text(output, layout->fabric->name);
	pigen_append(output, " — elaborated routing network</text>\n");

	for (size_t index = 0; index < layout->edge_count; index++)
	{
		svg_edge *edge = &layout->edges[index];
		svg_point left, right; svg_wire_points(layout, edge, &left, &right);
		pigen_append_format(output, "  <line class=\"edge %s\" data-kind=\"%s\" data-from=\"",
			edge->kind == 2 ? "direct" : "", svg_edge_kind_name(edge->kind));
		svg_append_node_name(output, layout, edge->left);
		pigen_append(output, "\" data-to=\"");
		svg_append_node_name(output, layout, edge->right);
		pigen_append(output, "\"");
		if (edge->kind == 2)
		{
			pigen_append(output, " data-source=\"");
			svg_append_endpoint_name(output,
				&layout->endpoints[edge->left_endpoint].endpoint);
			pigen_append(output, "\" data-destination=\"");
			svg_append_endpoint_name(output,
				&layout->endpoints[edge->right_endpoint].endpoint);
			pigen_append(output, "\"");
		}
		if (edge->kind == 1 || (edge->kind == 2 && !svg_direct_has_reverse(layout, edge)))
			pigen_append(output, " marker-end=\"url(#arrow)\"");
		pigen_append_format(output,
			" x1=\"%.3f\" y1=\"%.3f\" x2=\"%.3f\" y2=\"%.3f\"/>\n",
			left.x, left.y, right.x, right.y);
		pigen_append_format(output,
			"  <text class=\"port\" x=\"%.3f\" y=\"%.3f\" text-anchor=\"middle\">",
			(left.x + right.x) / 2.0, (left.y + right.y) / 2.0 - 5.0);
		svg_append_edge_label(output, edge); pigen_append(output, "</text>\n");
	}

	for (size_t unit = 0; unit < layout->unit_count; unit++)
	{
		int node = svg_unit_node(layout, (int)unit);
		pigen_append(output, "  <circle class=\"unit\" data-kind=\"unit\" data-unit=\"");
		svg_append_text(output, layout->units[unit].name);
		pigen_append_format(output, "\" cx=\"%.3f\" cy=\"%.3f\" r=\"%.3f\"/>\n",
			layout->positions[node].x, layout->positions[node].y, layout->units[unit].radius);
		pigen_append_format(output,
			"  <text class=\"label\" x=\"%.3f\" y=\"%.3f\" text-anchor=\"middle\">",
			layout->positions[node].x, layout->positions[node].y + 5.0);
		svg_append_text(output, layout->units[unit].name); pigen_append(output, "</text>\n");
		for (size_t endpoint = 0; endpoint < layout->endpoint_count; endpoint++)
			if (layout->endpoints[endpoint].unit == (int)unit)
			{
				svg_point port = svg_port_position(layout, (int)endpoint);
				pigen_append_format(output, "  <circle data-kind=\"%s-port\" data-endpoint=\"",
					layout->endpoints[endpoint].endpoint.direction ? "source" : "destination");
				svg_append_endpoint_data_name(output, &layout->endpoints[endpoint].endpoint);
				pigen_append_format(output,
					"\" cx=\"%.3f\" cy=\"%.3f\" r=\"4\" fill=\"#1d4ed8\"/>\n",
					port.x, port.y);
				pigen_append(output, "  <text class=\"port\" data-endpoint=\"");
				svg_append_endpoint_data_name(output, &layout->endpoints[endpoint].endpoint);
				pigen_append_format(output,
					"\" x=\"%.3f\" y=\"%.3f\" text-anchor=\"%s\">",
					layout->endpoints[endpoint].label_x,
					layout->endpoints[endpoint].label_y,
					layout->endpoints[endpoint].label_anchor > 0 ? "start" :
					layout->endpoints[endpoint].label_anchor < 0 ? "end" : "middle");
				char *label = svg_port_label(&layout->endpoints[endpoint].endpoint);
				svg_append_text(output, label); free(label); pigen_append(output, "</text>\n");
			}
	}
	for (int router = 0; router < layout->router_count; router++)
	{
		pigen_append_format(output,
			"  <circle class=\"router\" data-kind=\"router\" data-router=\"r%d\" cx=\"%.3f\" cy=\"%.3f\" r=\"25\"/>\n",
			router, layout->positions[router].x, layout->positions[router].y);
		pigen_append_format(output,
			"  <text x=\"%.3f\" y=\"%.3f\" text-anchor=\"middle\">r%d</text>\n",
			layout->positions[router].x, layout->positions[router].y + 5.0, router);
	}

	pigen_append_format(output,
		"  <text class=\"legend\" x=\"24\" y=\"%d\">connections:</text>\n", legend_y);
	const fabric_connection **connections = pigen_resize(NULL,
		layout->fabric->connection_count * sizeof(*connections));
	for (size_t index = 0; index < layout->fabric->connection_count; index++)
		connections[index] = &layout->fabric->connections[index];
	qsort(connections, layout->fabric->connection_count, sizeof(*connections),
		svg_connection_pointer_compare);
	for (size_t index = 0; index < layout->fabric->connection_count; index++)
	{
		const fabric_connection *connection = connections[index];
		pigen_append_format(output,
			"  <text class=\"legend\" x=\"45\" y=\"%d\">", legend_y + 20 * ((int)index + 1));
		svg_append_endpoint_name(output,
			&(fabric_endpoint){.direction = 1, .source = connection->source});
		pigen_append(output, " "); svg_append_connection_arrow(output, connection);
		pigen_append(output, " ");
		svg_append_endpoint_name(output,
			&(fabric_endpoint){.direction = 0, .destination = connection->destination});
		if (connection->direct) pigen_append(output, " (direct)");
		else
		{
			pigen_append_format(output, " (hops=%d path=0b", connection->hops);
			svg_append_binary_path(output, connection->path_word, layout->topology->path_width);
			pigen_append(output, ")");
		}
		pigen_append(output, "</text>\n");
	}
	free(connections);
	pigen_append(output, "</svg>\n");
}

static void render_fabric_diagram(pigen_string *output, const fabric_block *fabric,
	const fabric_topology *topology)
{
	svg_layout layout = {.fabric = fabric, .topology = topology};
	int width, network_height;
	svg_build_model(&layout);
	svg_seed_initial_positions(&layout);
	svg_relax_layout(&layout);
	svg_pack_components(&layout);
	int crossings_before = svg_minimize_crossings(&layout);
	int crossings = svg_crossing_count(&layout);
	svg_fit_viewport(&layout, &width, &network_height);
	svg_place_ports_and_labels(&layout);
	svg_render_output(output, &layout, width, network_height,
		crossings_before, crossings);
	free(layout.positions);
	free(layout.edges);
	free(layout.endpoints);
	free(layout.units);
}

#undef SVG_TAU
#undef SVG_PI
