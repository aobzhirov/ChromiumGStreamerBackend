# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import operator
import os
import sys
import unittest

import graph


class _IndexedNode(graph.Node):
  def __init__(self, index):
    super(_IndexedNode, self).__init__()
    self.index = index


class GraphTestCase(unittest.TestCase):
  @classmethod
  def MakeGraph(cls, count, edge_tuples):
    """Makes a graph from a list of edges.

    Args:
      count: Number of nodes.
      edge_tuples: (from_index, to_index). Both indices must be in [0, count),
                   and uniquely identify a node.
    """
    nodes = [_IndexedNode(i) for i in xrange(count)]
    edges = [graph.Edge(nodes[from_index], nodes[to_index])
             for (from_index, to_index) in edge_tuples]
    return (nodes, edges, graph.DirectedGraph(nodes, edges))

  @classmethod
  def _NodesIndices(cls, g):
    return map(operator.attrgetter('index'), g.Nodes())

  def testBuildGraph(self):
    (nodes, edges, g) = self.MakeGraph(
        7,
        [(0, 1),
         (0, 2),
         (1, 3),
         (3, 4),
         (5, 6)])
    self.assertListEqual(range(7), sorted(self._NodesIndices(g)))
    self.assertSetEqual(set(edges), set(g.Edges()))

    self.assertSetEqual(set([edges[0], edges[1]]), set(g.OutEdges(nodes[0])))
    self.assertFalse(g.InEdges(nodes[0]))
    self.assertSetEqual(set([edges[2]]), set(g.OutEdges(nodes[1])))
    self.assertSetEqual(set([edges[0]]), set(g.InEdges(nodes[1])))
    self.assertFalse(g.OutEdges(nodes[2]))
    self.assertSetEqual(set([edges[1]]), set(g.InEdges(nodes[2])))
    self.assertSetEqual(set([edges[3]]), set(g.OutEdges(nodes[3])))
    self.assertSetEqual(set([edges[2]]), set(g.InEdges(nodes[3])))
    self.assertFalse(g.OutEdges(nodes[4]))
    self.assertSetEqual(set([edges[3]]), set(g.InEdges(nodes[4])))
    self.assertSetEqual(set([edges[4]]), set(g.OutEdges(nodes[5])))
    self.assertFalse(g.InEdges(nodes[5]))
    self.assertFalse(g.OutEdges(nodes[6]))
    self.assertSetEqual(set([edges[4]]), set(g.InEdges(nodes[6])))

  def testIgnoresUnknownEdges(self):
    nodes = [_IndexedNode(i) for i in xrange(7)]
    edges = [graph.Edge(nodes[from_index], nodes[to_index])
             for (from_index, to_index) in [
                 (0, 1), (0, 2), (1, 3), (3, 4), (5, 6)]]
    edges.append(graph.Edge(nodes[4], _IndexedNode(42)))
    edges.append(graph.Edge(_IndexedNode(42), nodes[5]))
    g = graph.DirectedGraph(nodes, edges)
    self.assertListEqual(range(7), sorted(self._NodesIndices(g)))
    self.assertEqual(5, len(g.Edges()))

  def testUpdateEdge(self):
    (nodes, edges, g) = self.MakeGraph(
        7,
        [(0, 1),
         (0, 2),
         (1, 3),
         (3, 4),
         (5, 6)])
    edge = edges[1]
    self.assertTrue(edge in g.OutEdges(nodes[0]))
    self.assertTrue(edge in g.InEdges(nodes[2]))
    g.UpdateEdge(edge, nodes[2], nodes[3])
    self.assertFalse(edge in g.OutEdges(nodes[0]))
    self.assertFalse(edge in g.InEdges(nodes[2]))
    self.assertTrue(edge in g.OutEdges(nodes[2]))
    self.assertTrue(edge in g.InEdges(nodes[3]))

  def testTopologicalSort(self):
    (_, edges, g) = self.MakeGraph(
        7,
        [(0, 1),
         (0, 2),
         (1, 3),
         (3, 4),
         (5, 6)])
    sorted_nodes = g.TopologicalSort()
    node_to_sorted_index = dict(zip(sorted_nodes, xrange(len(sorted_nodes))))
    for e in edges:
      self.assertTrue(
          node_to_sorted_index[e.from_node] < node_to_sorted_index[e.to_node])

  def testReachableNodes(self):
    (nodes, _, g) = self.MakeGraph(
        7,
        [(0, 1),
         (0, 2),
         (1, 3),
         (3, 4),
         (5, 6)])
    self.assertSetEqual(
        set([0, 1, 2, 3, 4]),
        set(n.index for n in g.ReachableNodes([nodes[0]])))
    self.assertSetEqual(
        set([0, 1, 2, 3, 4]),
        set(n.index for n in g.ReachableNodes([nodes[0], nodes[1]])))
    self.assertSetEqual(
        set([5, 6]),
        set(n.index for n in g.ReachableNodes([nodes[5]])))
    self.assertSetEqual(
        set([6]),
        set(n.index for n in g.ReachableNodes([nodes[6]])))

  def testCost(self):
    (nodes, edges, g) = self.MakeGraph(
        7,
        [(0, 1),
         (0, 2),
         (1, 3),
         (3, 4),
         (5, 6)])
    for (i, node) in enumerate(nodes):
      node.cost = i + 1
    nodes[6].cost = 6
    for edge in edges:
      edge.cost = 1
    self.assertEqual(15, g.Cost())
    path_list = []
    g.Cost(path_list=path_list)
    self.assertListEqual([nodes[i] for i in (0, 1, 3, 4)], path_list)
    nodes[6].cost = 9
    self.assertEqual(16, g.Cost())
    g.Cost(path_list=path_list)
    self.assertListEqual([nodes[i] for i in (5, 6)], path_list)

  def testCostWithRoots(self):
    (nodes, edges, g) = self.MakeGraph(
        7,
        [(0, 1),
         (0, 2),
         (1, 3),
         (3, 4),
         (5, 6)])
    for (i, node) in enumerate(nodes):
      node.cost = i + 1
    nodes[6].cost = 9
    for edge in edges:
      edge.cost = 1
    path_list = []
    self.assertEqual(16, g.Cost(path_list=path_list))
    self.assertListEqual([nodes[i] for i in (5, 6)], path_list)
    self.assertEqual(15, g.Cost(roots=[nodes[0]], path_list=path_list))
    self.assertListEqual([nodes[i] for i in (0, 1, 3, 4)], path_list)


if __name__ == '__main__':
  unittest.main()
