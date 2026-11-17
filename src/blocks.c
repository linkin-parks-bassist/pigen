/* Balanced source-routed topology built from resolved module-port identities. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pigen/blocks.h"
#include "pigen/util.h"
typedef struct { char *instance; char *port;  } fabric_source;
typedef struct { char *instance; char *port; } fabric_destination;

typedef struct {
	fabric_source source;
	fabric_destination destination;
	int tier;
	int direct;
	unsigned long long path_word;
	unsigned long long delivered_word;
	int hops;
} fabric_connection;

typedef struct {
	int direction; /* 0 = destination, 1 = source (matching lexical order). */
	fabric_source source;
	fabric_destination destination;
} fabric_endpoint;

typedef struct {
	int kind; /* 0 = unset/dummy, 1 = router, 2 = endpoint */
	int target;
	int port;
} fabric_attachment;

typedef struct { fabric_attachment ports[3]; } fabric_router;

typedef struct {
	fabric_router *routers;
	size_t router_count;
	fabric_endpoint *endpoints;
	size_t endpoint_count;
	int *endpoint_router;
	int *endpoint_port;
	int path_width;
} fabric_topology;

typedef struct {
	char *name;
	fabric_connection *connections;
	size_t connection_count;
} fabric_block;

static int source_equal(const fabric_source *left, const fabric_source *right)
{
	return !strcmp(left->instance, right->instance) && !strcmp(left->port, right->port);
}

static int destination_equal(const fabric_destination *left, const fabric_destination *right)
{
	return !strcmp(left->instance, right->instance) && !strcmp(left->port, right->port);
}

static int endpoint_compare(const void *left_, const void *right_)
{
	const fabric_endpoint *left = left_;
	const fabric_endpoint *right = right_;
	int comparison;
	if (left->direction != right->direction) return left->direction - right->direction;
	if (!left->direction)
	{
		comparison = strcmp(left->destination.instance, right->destination.instance);
		return comparison ? comparison : strcmp(left->destination.port, right->destination.port);
	}
	comparison = strcmp(left->source.instance, right->source.instance);
	if (comparison) return comparison;
	comparison = strcmp(left->source.port, right->source.port);
	return comparison;
}

static int endpoint_equal(const fabric_endpoint *left, const fabric_endpoint *right)
{
	if (left->direction != right->direction) return 0;
	return left->direction ? source_equal(&left->source, &right->source) :
		destination_equal(&left->destination, &right->destination);
}

static void add_endpoint(fabric_topology *topology, fabric_endpoint endpoint)
{
	for (size_t index = 0; index < topology->endpoint_count; index++)
		if (endpoint_equal(&topology->endpoints[index], &endpoint)) return;
	topology->endpoints = pigen_resize(topology->endpoints,
		(topology->endpoint_count + 1) * sizeof(*topology->endpoints));
	topology->endpoints[topology->endpoint_count++] = endpoint;
}

static int endpoint_index(const fabric_topology *topology, const fabric_endpoint *endpoint)
{
	for (size_t index = 0; index < topology->endpoint_count; index++)
		if (endpoint_equal(&topology->endpoints[index], endpoint)) return (int)index;
	pigen_fail("internal fabric endpoint lookup failed");
	return -1;
}

typedef struct { int kind; int index; } topology_node;

static topology_node make_topology_parent(fabric_topology *topology, topology_node left, topology_node right)
{
	int router;
	if (!left.kind) return right;
	if (!right.kind) return left;
	router = (int)topology->router_count++;
	topology->routers = pigen_resize(topology->routers,
		topology->router_count * sizeof(*topology->routers));
	topology->routers[router] = (fabric_router){0};
	topology_node children[2] = {left, right};
	for (int child = 0; child < 2; child++)
	{
		int port = child + 1;
		if (children[child].kind == 1)
		{
			topology->routers[router].ports[port] = (fabric_attachment){1, children[child].index, 0};
			topology->routers[children[child].index].ports[0] = (fabric_attachment){1, router, port};
		}
		else
		{
			topology->routers[router].ports[port] = (fabric_attachment){2, children[child].index, 0};
			topology->endpoint_router[children[child].index] = router;
			topology->endpoint_port[children[child].index] = port;
		}
	}
	return (topology_node){1, router};
}

static int attachment_matches_node(const fabric_attachment *attachment, int router_count, int node)
{
	if (attachment->kind == 1) return attachment->target == node;
	if (attachment->kind == 2) return router_count + attachment->target == node;
	return 0;
}

static int port_for_node(const fabric_topology *topology, int router, int node)
{
	for (int port = 0; port < 3; port++)
		if (attachment_matches_node(&topology->routers[router].ports[port],
			(int)topology->router_count, node)) return port;
	pigen_fail("internal fabric route traverses non-neighbor nodes");
	return -1;
}

static unsigned long long rotate_right_word(unsigned long long value, int width, int count)
{
	unsigned long long mask;
	if (width <= 0) return 0;
	mask = width == 64 ? ~0ULL : (1ULL << width) - 1;
	for (int index = 0; index < count % width; index++)
		value = ((value >> 1) | ((value & 1) << (width - 1))) & mask;
	return value;
}

static int trace_forward_endpoint(const fabric_topology *topology, int endpoint,
	unsigned long long path_word)
{
	int router = topology->endpoint_router[endpoint];
	int in_port = topology->endpoint_port[endpoint];
	for (size_t hop = 0; hop <= topology->router_count; hop++)
	{
		int bit = (int)(path_word & 1ULL);
		int out_port = bit ? (in_port + 2) % 3 : (in_port + 1) % 3;
		fabric_attachment attachment = topology->routers[router].ports[out_port];
		path_word = rotate_right_word(path_word, topology->path_width, 1);
		if (attachment.kind == 2) return attachment.target;
		if (attachment.kind != 1) pigen_fail("generated fabric route reached a dummy port");
		router = attachment.target;
		in_port = attachment.port;
	}
	pigen_fail("generated fabric route did not terminate");
	return -1;
}

static unsigned long long rotate_left_word(unsigned long long value, int width)
{
	unsigned long long mask;
	if (width <= 0) return 0;
	mask = width == 64 ? ~0ULL : (1ULL << width) - 1;
	return ((value << 1) | (value >> (width - 1))) & mask;
}

static int trace_reverse_endpoint(const fabric_topology *topology, int endpoint,
	unsigned long long delivered_word)
{
	int router = topology->endpoint_router[endpoint];
	int out_port = topology->endpoint_port[endpoint];
	for (size_t hop = 0; hop <= topology->router_count; hop++)
	{
		int bit = (int)((delivered_word >> (topology->path_width - 1)) & 1ULL);
		int in_port = bit ? (out_port + 1) % 3 : (out_port + 2) % 3;
		fabric_attachment attachment = topology->routers[router].ports[in_port];
		delivered_word = rotate_left_word(delivered_word, topology->path_width);
		if (attachment.kind == 2) return attachment.target;
		if (attachment.kind != 1) pigen_fail("generated reverse fabric route reached a dummy port");
		router = attachment.target;
		out_port = attachment.port;
	}
	pigen_fail("generated reverse fabric route did not terminate");
	return -1;
}

static void route_connection(fabric_topology *topology, fabric_connection *connection)
{
	fabric_endpoint source = {.direction = 1, .source = connection->source};
	fabric_endpoint destination = {.direction = 0, .destination = connection->destination};
	int source_index = endpoint_index(topology, &source);
	int destination_index_ = endpoint_index(topology, &destination);
	int node_count = (int)(topology->router_count + topology->endpoint_count);
	int start = (int)topology->router_count + source_index;
	int finish = (int)topology->router_count + destination_index_;
	int *previous = pigen_resize(NULL, (size_t)node_count * sizeof(*previous));
	int *queue = pigen_resize(NULL, (size_t)node_count * sizeof(*queue));
	int *path = pigen_resize(NULL, (size_t)node_count * sizeof(*path));
	int head = 0, tail = 0, path_count = 0;
	for (int index = 0; index < node_count; index++) previous[index] = -2;
	previous[start] = -1; queue[tail++] = start;
	while (head < tail)
	{
		int node = queue[head++];
		if (node == finish) break;
		if (node < (int)topology->router_count)
		{
			for (int port = 0; port < 3; port++)
			{
				fabric_attachment *attachment = &topology->routers[node].ports[port];
				int next;
				if (!attachment->kind) continue;
				next = attachment->kind == 1 ? attachment->target :
					(int)topology->router_count + attachment->target;
				if (previous[next] == -2) { previous[next] = node; queue[tail++] = next; }
			}
		}
		else
		{
			int endpoint = node - (int)topology->router_count;
			int next = topology->endpoint_router[endpoint];
			if (previous[next] == -2) { previous[next] = node; queue[tail++] = next; }
		}
	}
	if (previous[finish] == -2) pigen_fail("fabric topology did not connect route endpoints");
	for (int node = finish; node != -1; node = previous[node]) path[path_count++] = node;
	for (int left = 0, right = path_count - 1; left < right; left++, right--)
	{
		int temporary = path[left]; path[left] = path[right]; path[right] = temporary;
	}
	connection->path_word = 0;
	connection->hops = 0;
	for (int index = 1; index + 1 < path_count; index++)
	{
		int router = path[index];
		int in_port = port_for_node(topology, router, path[index - 1]);
		int out_port = port_for_node(topology, router, path[index + 1]);
		int bit = 0;
		if (out_port == (in_port + 1) % 3) bit = 0;
		else if (out_port == (in_port + 2) % 3) bit = 1;
		else pigen_fail("fabric route attempted a U-turn");
		if (connection->hops >= 64) pigen_fail("fabric route exceeds the 64-bit path limit");
		connection->path_word |= (unsigned long long)bit << connection->hops++;
	}
	free(previous); free(queue); free(path);
}

static fabric_topology build_fabric_topology(fabric_block *fabric)
{
	fabric_topology topology = {0};
	size_t leaf_count = 1;
	topology_node *level;
	size_t level_count;
	for (size_t index = 0; index < fabric->connection_count; index++)
	{
		fabric_connection *connection = &fabric->connections[index];
		if (connection->direct) continue;
		add_endpoint(&topology, (fabric_endpoint){.direction = 1, .source = connection->source});
		add_endpoint(&topology, (fabric_endpoint){.direction = 0, .destination = connection->destination});
	}
	if (!topology.endpoint_count) return topology;
	qsort(topology.endpoints, topology.endpoint_count, sizeof(*topology.endpoints), endpoint_compare);
	topology.endpoint_router = pigen_resize(NULL, topology.endpoint_count * sizeof(*topology.endpoint_router));
	topology.endpoint_port = pigen_resize(NULL, topology.endpoint_count * sizeof(*topology.endpoint_port));
	for (size_t index = 0; index < topology.endpoint_count; index++) topology.endpoint_router[index] = topology.endpoint_port[index] = -1;
	while (leaf_count < topology.endpoint_count) leaf_count *= 2;
	level = pigen_resize(NULL, leaf_count * sizeof(*level)); level_count = leaf_count;
	for (size_t index = 0; index < leaf_count; index++)
		level[index] = index < topology.endpoint_count ? (topology_node){2, (int)index} : (topology_node){0, 0};
	while (level_count > 1)
	{
		for (size_t index = 0; index < level_count; index += 2)
			level[index / 2] = make_topology_parent(&topology, level[index], level[index + 1]);
		level_count /= 2;
	}
	if (level[0].kind != 1) pigen_fail("routed fabric requires at least two endpoints");
	topology.routers[level[0].index].ports[0] = (fabric_attachment){0, 0, 0};
	free(level);
	for (size_t index = 0; index < fabric->connection_count; index++)
		if (!fabric->connections[index].direct)
		{
			route_connection(&topology, &fabric->connections[index]);
			if (fabric->connections[index].hops > topology.path_width)
				topology.path_width = fabric->connections[index].hops;
		}
	for (size_t index = 0; index < fabric->connection_count; index++)
		if (!fabric->connections[index].direct)
			fabric->connections[index].delivered_word = rotate_right_word(
				fabric->connections[index].path_word, topology.path_width,
				fabric->connections[index].hops);
	for (size_t index = 0; index < fabric->connection_count; index++)
	{
		fabric_connection *connection = &fabric->connections[index];
		fabric_endpoint source = {.direction = 1, .source = connection->source};
		fabric_endpoint destination = {.direction = 0, .destination = connection->destination};
		int source_index;
		int destination_index_;
		if (connection->direct) continue;
		source_index = endpoint_index(&topology, &source);
		destination_index_ = endpoint_index(&topology, &destination);
		if (trace_forward_endpoint(&topology, source_index, connection->path_word) != destination_index_)
			pigen_fail("generated fabric route did not reach its destination");
		if (trace_reverse_endpoint(&topology, destination_index_, connection->delivered_word) != source_index)
			pigen_fail("generated fabric route is not reversible");
		for (size_t previous = 0; previous < index; previous++)
			if (!fabric->connections[previous].direct &&
				destination_equal(&fabric->connections[previous].destination, &connection->destination) &&
				fabric->connections[previous].delivered_word == connection->delivered_word)
				pigen_fail("generated fabric routes collide at a destination");
	}
	return topology;
}

#include "fabric_svg.inc"


int pigen_build_fabric_tree(const char *name,
 const pigen_fabric_port *ports, size_t port_count,
 const pigen_fabric_edge *edges, size_t edge_count, pigen_fabric_tree *tree)
{
 if(!tree || !name || !edge_count || !ports || !edges) return 0;
 *tree=(pigen_fabric_tree){0};
 fabric_block fabric={.name=(char *)name};
 fabric.connections=pigen_resize(NULL,edge_count*sizeof(*fabric.connections));
 fabric.connection_count=edge_count;
 for(size_t i=0;i<edge_count;i++) {
  if(edges[i].source>=port_count || edges[i].destination>=port_count) {free(fabric.connections);return 0;}
  const pigen_fabric_port *s=&ports[edges[i].source],*d=&ports[edges[i].destination];
  fabric.connections[i]=(fabric_connection){.source={(char *)s->instance,(char *)s->port},
   .destination={(char *)d->instance,(char *)d->port},.direct=edges[i].direct,.tier=(int)edges[i].tier};
 }
 fabric_topology topology=build_fabric_topology(&fabric);
 tree->router_count=topology.router_count;tree->port_count=port_count;
 tree->path_width=(size_t)topology.path_width;
 tree->routers=pigen_resize(NULL,topology.router_count*sizeof(*tree->routers));
 tree->port_router=pigen_resize(NULL,port_count*sizeof(*tree->port_router));
 tree->port_number=pigen_resize(NULL,port_count*sizeof(*tree->port_number));
 tree->paths=pigen_resize(NULL,edge_count*sizeof(*tree->paths));
 tree->hops=pigen_resize(NULL,edge_count*sizeof(*tree->hops));
 size_t *map=pigen_resize(NULL,topology.endpoint_count*sizeof(*map));
 for(size_t i=0;i<port_count;i++) tree->port_router[i]=tree->port_number[i]=-1;
 for(size_t i=0;i<topology.endpoint_count;i++) {
  const fabric_endpoint *e=&topology.endpoints[i];
  const char *instance=e->direction ? e->source.instance:e->destination.instance;
  const char *port=e->direction ? e->source.port:e->destination.port;
  size_t j=0;for(;j<port_count;j++) if(ports[j].source==e->direction &&
    !strcmp(ports[j].instance,instance) && !strcmp(ports[j].port,port)) break;
  if(j==port_count) pigen_fail("fabric topology lost resolved endpoint");
  map[i]=j;tree->port_router[j]=topology.endpoint_router[i];tree->port_number[j]=topology.endpoint_port[i];
 }
 for(size_t r=0;r<topology.router_count;r++) for(size_t p=0;p<3;p++) {
  fabric_attachment a=topology.routers[r].ports[p];
  tree->routers[r].ports[p]=(pigen_fabric_attachment){a.kind,a.kind==2 ? map[a.target]:(size_t)a.target,(size_t)a.port};
 }
 for(size_t i=0;i<edge_count;i++) {tree->paths[i]=fabric.connections[i].path_word;tree->hops[i]=(size_t)fabric.connections[i].hops;}
 pigen_string svg={0};render_fabric_diagram(&svg,&fabric,&topology);tree->svg=svg.data;
 free(map);free(topology.routers);free(topology.endpoints);free(topology.endpoint_router);free(topology.endpoint_port);free(fabric.connections);
 return 1;
}
void pigen_free_fabric_tree(pigen_fabric_tree *tree)
{
 if(!tree)return;
 free(tree->routers);free(tree->port_router);free(tree->port_number);free(tree->paths);free(tree->hops);free(tree->svg);
 *tree=(pigen_fabric_tree){0};
}
void pigen_free_fabric_diagrams(pigen_fabric_diagrams *diagrams)
{
	if (!diagrams) return;
	for (size_t index = 0; index < diagrams->count; index++)
	{
		free(diagrams->items[index].name);
		free(diagrams->items[index].svg);
	}
	free(diagrams->items);
	*diagrams = (pigen_fabric_diagrams){0};
}
