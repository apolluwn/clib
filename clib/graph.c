#include <clib/graph.h>

/* Set link distance, creating link if not found. */
u32 graph_set_link (graph_t * g, u32 src, u32 dst, u32 distance)
{
  graph_node_t * src_node, * dst_node;
  graph_link_t * l;
  u32 old_distance;
  
  /* The following validate will not work if src or dst are on the
     pool free list. */
  if (src < vec_len (g->nodes))
    ASSERT (! pool_is_free_index (g->nodes, src));
  if (dst < vec_len (g->nodes))
    ASSERT (! pool_is_free_index (g->nodes, dst));

  /* Make new (empty) nodes to make src and dst valid. */
  pool_validate_index (g->nodes, clib_max (src, dst));

  src_node = pool_elt_at_index (g->nodes, src);
  dst_node = pool_elt_at_index (g->nodes, dst);

  l = graph_dir_get_link_to_node (&src_node->next, dst);
  if (l)
    {
      old_distance = l->distance;
      l->distance = distance;

      l = graph_dir_get_link_to_node (&dst_node->prev, src);
      l->distance = distance;
    }
  else
    {
      uword li_next, li_prev;

      old_distance = ~0;

      li_next = graph_dir_add_link (&src_node->next, dst, distance);
      li_prev = graph_dir_add_link (&dst_node->prev, src, distance);

      l = vec_elt_at_index (src_node->next.links, li_next);
      l->link_to_self_index = li_prev;

      l = vec_elt_at_index (dst_node->prev.links, li_prev);
      l->link_to_self_index = li_next;
    }

  return old_distance;
}

void graph_del_link (graph_t * g, u32 src, u32 dst)
{
  graph_node_t * src_node, * dst_node;
  
  src_node = pool_elt_at_index (g->nodes, src);
  dst_node = pool_elt_at_index (g->nodes, dst);

  graph_dir_del_link (&src_node->next, dst);
  graph_dir_del_link (&dst_node->next, src);
}

/* Delete source node and all links from other nodes from/to source. */
uword graph_del_node (graph_t * g, u32 src)
{
  graph_node_t * src_node, * n;
  uword index;
  graph_link_t * l;

  src_node = pool_elt_at_index (g->nodes, src);

  vec_foreach (l, src_node->next.links)
    {
      n = pool_elt_at_index (g->nodes, l->node_index);
      graph_dir_del_link (&n->prev, src);
    }

  vec_foreach (l, src_node->prev.links)
    {
      n = pool_elt_at_index (g->nodes, l->node_index);
      graph_dir_del_link (&n->next, src);
    }

  graph_dir_free (&src_node->next);
  graph_dir_free (&src_node->prev);

  index = n - g->nodes;
  pool_put (g->nodes, n);
  memset (n, ~0, sizeof (n[0]));

  return index;
}

uword unformat_graph (unformat_input_t * input, va_list * args)
{
  graph_t * g = va_arg (*args, graph_t *);
  typedef struct {
    u32 src, dst, distance;
  } T;
  T * links = 0, * l;
  uword result;

  while (1)
    {
      vec_add2 (links, l, 1);
      if (! unformat (input, "%d%d%d", &l->src, &l->dst, &l->distance))
	break;
    }
  _vec_len (links) -= 1;
  result = vec_len (links) > 0;
  vec_foreach (l, links)
    {
      graph_set_link (g, l->src, l->dst, l->distance);
      graph_set_link (g, l->dst, l->src, l->distance);
    }

  vec_free (links);
  return result;
}

u8 * format_graph (u8 * s, va_list * args)
{
  graph_t * g = va_arg (*args, graph_t *);
  graph_node_t * n;
  graph_link_t * l;
  uword indent = format_get_indent (s);

  s = format (s, "graph %d nodes", pool_elts (g->nodes));
  pool_foreach (n, g->nodes, ({
    s = format (s, "\n%U", format_white_space, indent + 2);
    s = format (s, "%d -> ", n - g->nodes);
    vec_foreach (l, n->next.links)
      s = format (s, "%d (%d), ", l->node_index, l->distance);
  }));

  return s;
}
