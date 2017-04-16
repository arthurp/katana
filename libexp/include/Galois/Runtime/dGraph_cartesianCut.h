/** partitioned graph wrapper for cartesianCut -*- C++ -*-
 * @file
 * @section License
 *
 * Galois, a framework to exploit amorphous data-parallelism in irregular
 * programs.
 *
 * Copyright (C) 2013, The University of Texas at Austin. All rights reserved.
 * UNIVERSITY EXPRESSLY DISCLAIMS ANY AND ALL WARRANTIES CONCERNING THIS
 * SOFTWARE AND DOCUMENTATION, INCLUDING ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR ANY PARTICULAR PURPOSE, NON-INFRINGEMENT AND WARRANTIES OF
 * PERFORMANCE, AND ANY WARRANTY THAT MIGHT OTHERWISE ARISE FROM COURSE OF
 * DEALING OR USAGE OF TRADE.  NO WARRANTY IS EITHER EXPRESS OR IMPLIED WITH
 * RESPECT TO THE USE OF THE SOFTWARE OR DOCUMENTATION. Under no circumstances
 * shall University be liable for incidental, special, indirect, direct or
 * consequential damages or loss of profits, interruption of business, or
 * related expenses which may arise from use of Software or Documentation,
 * including but not limited to those resulting from defects in Software and/or
 * Documentation, or loss or inaccuracy of data of any kind.
 *
 * @section Contains the cartesian/grid vertex-cut functionality to be used in dGraph.
 *
 * @author Roshan Dathathri <roshan@cs.utexas.edu>
 */

#include <vector>
#include <set>
#include <algorithm>
#include <unordered_map>
#include "Galois/Runtime/dGraph.h"
#include "Galois/Runtime/OfflineGraph.h"
#include "Galois/Runtime/Serialize.h"

template<typename NodeTy, typename EdgeTy, bool BSPNode = false, bool BSPEdge = false>
class hGraph_cartesianCut : public hGraph<NodeTy, EdgeTy, BSPNode, BSPEdge> {
public:
  typedef hGraph<NodeTy, EdgeTy, BSPNode, BSPEdge> base_hGraph;

private:
  uint32_t numRowHosts;
  uint32_t numColumnHosts;
  uint32_t blockSize; // number of nodes in a (square) block
  uint32_t numRowBlocksPerHost; // same as numColumnHosts
  uint32_t numColumnBlocksPerHost; // same as numRowHosts

  uint32_t dummyOwnedNodes; // owned nodes without outgoing edges

  // factorize numHosts such that difference between factors is minimized
  void factorize_hosts() {
    numColumnHosts = sqrt(base_hGraph::numHosts);
    while ((base_hGraph::numHosts % numColumnHosts) != 0) numColumnHosts--;
    numRowHosts = base_hGraph::numHosts/numColumnHosts;
    assert(numRowHosts>=numColumnHosts);
    blockSize = (base_hGraph::totalNodes + base_hGraph::numHosts - 1)/ base_hGraph::numHosts;
    numRowBlocksPerHost = numColumnHosts;
    numColumnBlocksPerHost = numRowHosts;
    if (base_hGraph::id == 0) {
      std::cerr << "Cartesian grid: " << numRowHosts << " x " << numColumnHosts << "\n";
    }
  }

  uint32_t gridRowID() {
    return (base_hGraph::id / numColumnHosts);
  }

  uint32_t gridRowID(unsigned id) {
    return (id / numColumnHosts);
  }

  uint32_t gridColumnID() {
    return (base_hGraph::id % numColumnHosts);
  }

  uint32_t gridColumnID(unsigned id) {
    return (id % numColumnHosts);
  }

  // called only for those hosts with which it shares nodes
  bool isNotCommunicationPartner(unsigned host, typename base_hGraph::SyncType syncType, typename base_hGraph::DataflowDirection dataFlow) {
    if (syncType == base_hGraph::syncPush) {
      switch(dataFlow) {
        case base_hGraph::forwardFlow:
          return (gridColumnID() != gridColumnID(host));
        case base_hGraph::backwardFlow:
          return (gridRowID() != gridRowID(host));
        case base_hGraph::exchangeFlow:
          return false;
        default:
          assert(false);
      }
    } else { // syncPull
      switch(dataFlow) {
        case base_hGraph::forwardFlow:
          return (gridRowID() != gridRowID(host));
        case base_hGraph::backwardFlow:
          return (gridColumnID() != gridColumnID(host));
        case base_hGraph::exchangeFlow:
          return false;
        default:
          assert(false);
      }
    }
    return false;
  }


public:
  // GID = localToGlobalVector[LID]
  std::vector<uint64_t> localToGlobalVector;
  // LID = globalToLocalMap[GID]
  std::unordered_map<uint64_t, uint32_t> globalToLocalMap;
  //LID Node owned by host i. Stores ghost nodes from each host.
  //std::vector<std::pair<uint32_t, uint32_t> > hostNodes; // for reading graph only
  std::vector<std::pair<uint64_t, uint64_t>> gid2host; // for reading graph only

  uint32_t numNodes;
  uint64_t numEdges;

  // TODO: support scalefactor
  virtual unsigned getHostID(uint64_t gid) const {
    assert(gid < base_hGraph::totalNodes);
    uint32_t blockID = gid / blockSize;
    uint32_t rowID = blockID / numRowBlocksPerHost;
    uint32_t columnID = blockID / numColumnBlocksPerHost;
    return (rowID * numColumnHosts) + columnID;
  }

  virtual bool isOwned(uint64_t gid) const {
    if (gid >= base_hGraph::totalNodes) return false;
    return getHostID(gid) == base_hGraph::id;
  }

  virtual bool isLocal(uint64_t gid) const {
    assert(gid < base_hGraph::totalNodes);
    if (isOwned(gid))
      return true;
    return (globalToLocalMap.find(gid) != globalToLocalMap.end());
  }

  virtual uint32_t G2L(uint64_t gid) const {
    assert(isLocal(gid));
    return globalToLocalMap.at(gid);
  }

  virtual uint64_t L2G(uint32_t lid) const {
    return localToGlobalVector[lid];
  }

  // requirement: for all X and Y,
  // On X, nothingToSend(Y) <=> On Y, nothingToRecv(X)
  // Note: templates may not be virtual, so passing types as arguments
  virtual bool nothingToSend(unsigned host, typename base_hGraph::SyncType syncType, typename base_hGraph::DataflowDirection dataFlow) { // ignore dataflow direction
    auto &sharedNodes = (syncType == base_hGraph::syncPush) ? base_hGraph::slaveNodes : base_hGraph::masterNodes;
    if (sharedNodes[host].size() > 0) {
      return isNotCommunicationPartner(host, syncType, dataFlow);
    }
    return true;
  }
  virtual bool nothingToRecv(unsigned host, typename base_hGraph::SyncType syncType, typename base_hGraph::DataflowDirection dataFlow) { // ignore dataflow direction
    auto &sharedNodes = (syncType == base_hGraph::syncPush) ? base_hGraph::masterNodes : base_hGraph::slaveNodes;
    if (sharedNodes[host].size() > 0) {
      return isNotCommunicationPartner(host, syncType, dataFlow);
    }
    return true;
  }

  hGraph_cartesianCut(const std::string& filename, const std::string& partitionFolder, unsigned host, unsigned _numHosts, std::vector<unsigned> scalefactor, bool transpose = false) : base_hGraph(host, _numHosts) /*, uint32_t& _numNodes, uint32_t& _numOwned,uint64_t& _numEdges, uint64_t& _totalNodes, unsigned _id )*/{

    Galois::Statistic statGhostNodes("TotalGhostNodes");
    Galois::StatTimer StatTimer_graph_construct("TIME_GRAPH_CONSTRUCT");
    StatTimer_graph_construct.start();
    Galois::StatTimer StatTimer_graph_construct_comm("TIME_GRAPH_CONSTRUCT_COMM");
    Galois::Graph::OfflineGraph g(filename);

    base_hGraph::totalNodes = g.size();
    if (base_hGraph::id == 0) {
      std::cerr << "Total nodes : " << base_hGraph::totalNodes << "\n";
    }
    factorize_hosts();

    // compute readers for all nodes
    // if (scalefactor.empty() || (base_hGraph::numHosts == 1)) {
      for (unsigned i = 0; i < base_hGraph::numHosts; ++i)
        gid2host.push_back(Galois::block_range(0U, (unsigned) g.size(), i, base_hGraph::numHosts));
    if (!scalefactor.empty()) {
      if (base_hGraph::id == 0) {
        std::cerr << "WARNING: scalefactor not supported for vertex-cuts\n";
      }
    }
#if 0
    } else {
      assert(scalefactor.size() == base_hGraph::numHosts);
      unsigned numBlocks = 0;
      for (unsigned i = 0; i < base_hGraph::numHosts; ++i)
        numBlocks += scalefactor[i];
      std::vector<std::pair<uint64_t, uint64_t>> blocks;
      for (unsigned i = 0; i < numBlocks; ++i)
        blocks.push_back(Galois::block_range(0U, (unsigned) g.size(), i, numBlocks));
      std::vector<unsigned> prefixSums;
      prefixSums.push_back(0);
      for (unsigned i = 1; i < base_hGraph::numHosts; ++i)
        prefixSums.push_back(prefixSums[i - 1] + scalefactor[i - 1]);
      for (unsigned i = 0; i < base_hGraph::numHosts; ++i) {
        unsigned firstBlock = prefixSums[i];
        unsigned lastBlock = prefixSums[i] + scalefactor[i] - 1;
        gid2host.push_back(std::make_pair(blocks[firstBlock].first, blocks[lastBlock].second));
      }
    }
#endif

    std::vector<uint32_t> prefixSumOfEdges;
    loadStatistics(g, prefixSumOfEdges); // first pass of the graph file

    std::cerr << "[" << base_hGraph::id << "] Owned nodes: " << base_hGraph::totalOwnedNodes << "\n";
    std::cerr << "[" << base_hGraph::id << "] Ghost nodes: " << numNodes - base_hGraph::totalOwnedNodes << "\n";
    std::cerr << "[" << base_hGraph::id << "] Nodes which have edges: " << base_hGraph::numOwned << "\n";
    std::cerr << "[" << base_hGraph::id << "] Total edges : " << numEdges << "\n";

    if (numNodes > 0) {
      assert(numEdges > 0);
      base_hGraph::graph.allocateFrom(numNodes, numEdges);
      //std::cerr << "Allocate done\n";

      base_hGraph::graph.constructNodes();

      //std::cerr << "Construct nodes done\n";
      for (uint32_t n = 0; n < numNodes; ++n) {
        base_hGraph::graph.fixEndEdge(n, prefixSumOfEdges[n]);
      }
    }

    loadEdges(base_hGraph::graph, g); // second pass of the graph file
    std::cerr << "[" << base_hGraph::id << "] Edges loaded \n";

    if (transpose && (numNodes > 0)) {
      base_hGraph::graph.transpose();
      base_hGraph::transposed = true;
    }

    fill_slaveNodes(base_hGraph::slaveNodes);
    StatTimer_graph_construct.stop();

    StatTimer_graph_construct_comm.start();
    base_hGraph::setup_communication();
    StatTimer_graph_construct_comm.stop();
  }

  void loadStatistics(Galois::Graph::OfflineGraph& g, std::vector<uint32_t>& prefixSumOfEdges) {
    auto columnBlockSize = numColumnBlocksPerHost * blockSize;
    std::vector<Galois::DynamicBitSet> hasIncomingEdge(numColumnHosts);
    for (unsigned i = 0; i < numColumnHosts; ++i) {
      hasIncomingEdge[i].resize(columnBlockSize);
    }
    std::vector<uint64_t> columnOffsets(numColumnHosts);
    columnOffsets[0] = 0;
    for (unsigned i = 1; i < numColumnHosts; ++i) {
      columnOffsets[i] = columnOffsets[i-1] + columnBlockSize;
    }

    auto rowBlockSize = numRowBlocksPerHost * blockSize;
    std::vector<std::vector<uint32_t> > numOutgoingEdges(numColumnHosts);
    for (unsigned i = 0; i < numColumnHosts; ++i) {
      numOutgoingEdges[i].assign(blockSize, 0);
    }
    uint64_t rowOffset = gid2host[base_hGraph::id].first;

    auto ee = g.edge_begin(gid2host[base_hGraph::id].first);
    for (auto src = gid2host[base_hGraph::id].first; src < gid2host[base_hGraph::id].second; ++src) {
      auto ii = ee;
      ee = g.edge_end(src);
      for (; ii < ee; ++ii) {
        auto dst = g.getEdgeDst(ii);
        for (int i = numColumnHosts - 1; i >= 0; --i) {
          if (dst >= columnOffsets[i]) {
            hasIncomingEdge[i].set(dst - columnOffsets[i]);
            numOutgoingEdges[i][src - rowOffset]++;
            break;
          }
        }
      }
    }

    auto& net = Galois::Runtime::getSystemNetworkInterface();
    for (unsigned i = 0; i < numColumnHosts; ++i) {
      unsigned h = (gridRowID() * numColumnHosts) + i;
      if (h == base_hGraph::id) {
        columnOffsets[0] = columnOffsets[i];
        continue;
      }
      Galois::Runtime::SendBuffer b;
      Galois::Runtime::gSerialize(b, numOutgoingEdges[i]);
      Galois::Runtime::gSerialize(b, hasIncomingEdge[i]);
      net.sendTagged(h, Galois::Runtime::evilPhase, b);
    }
    net.flush();

    assert(numColumnHosts == numRowBlocksPerHost);
    for (unsigned i = 1; i < numRowBlocksPerHost; ++i) {
      decltype(net.recieveTagged(Galois::Runtime::evilPhase, nullptr)) p;
      do {
        net.handleReceives();
        p = net.recieveTagged(Galois::Runtime::evilPhase, nullptr);
      } while (!p);
      unsigned h = (p->first % numColumnHosts);
      auto& b = p->second;
      Galois::Runtime::gDeserialize(b, numOutgoingEdges[h]);
      Galois::Runtime::gDeserialize(b, hasIncomingEdge[h]);
    }
    ++Galois::Runtime::evilPhase;

    for (unsigned i = 1; i < numColumnHosts; ++i) {
      hasIncomingEdge[0].bitwise_or(hasIncomingEdge[i]);
    }

    auto max_nodes = (numOutgoingEdges[0].size() * numColumnHosts) + hasIncomingEdge[0].size();
    localToGlobalVector.reserve(max_nodes);
    globalToLocalMap.reserve(max_nodes);
    prefixSumOfEdges.reserve(max_nodes);
    rowOffset = gridRowID() * rowBlockSize;
    uint64_t src = rowOffset;
    numNodes = 0;
    numEdges = 0;
    dummyOwnedNodes = 0;
    for (unsigned i = 0; i < numColumnHosts; ++i) {
      for (uint32_t j = 0; j < numOutgoingEdges[i].size(); ++j) {
        bool createNode = false;
        if (numOutgoingEdges[i][j] > 0) {
          createNode = true;
          numEdges += numOutgoingEdges[i][j];
        } else if (isOwned(src)) {
          createNode = true;
          ++dummyOwnedNodes;
        } else if ((src >= columnOffsets[0]) 
          && (src < (columnOffsets[0] + columnBlockSize)) 
          && hasIncomingEdge[0].test(src - columnOffsets[0])) {
          createNode = true;
        }
        if (createNode) {
          localToGlobalVector.push_back(src);
          globalToLocalMap[src] = numNodes++;
          prefixSumOfEdges.push_back(numEdges);
        }
        ++src;
      }
    }
    base_hGraph::numOwned = numNodes; // number of nodes for which there are outgoing edges
    base_hGraph::totalOwnedNodes = 0;
    for (uint32_t i = 0; i < numColumnBlocksPerHost; ++i) {
      auto dst = columnOffsets[0] + (i * blockSize);
      auto dst_end = dst + blockSize;
      if ((dst_end <= rowOffset) || (dst >= (rowOffset + rowBlockSize))) {
        for (; dst < dst_end; ++dst) {
          if (hasIncomingEdge[0].test(dst - columnOffsets[0])) {
            localToGlobalVector.push_back(dst);
            globalToLocalMap[dst] = numNodes++;
            prefixSumOfEdges.push_back(numEdges);
          }
        }
      } else { // also a source
        base_hGraph::totalOwnedNodes += blockSize;
      }
    }
  }

  template<typename GraphTy>
  void loadEdges(GraphTy& graph, Galois::Graph::OfflineGraph& g) {
    if (base_hGraph::id == 0) {
      if (std::is_void<typename GraphTy::edge_data_type>::value) {
        fprintf(stderr, "Loading void edge-data while creating edges.\n");
      } else {
        fprintf(stderr, "Loading edge-data while creating edges.\n");
      }
    }

    unsigned h_offset = gridRowID() * numColumnHosts;
    static std::vector<Galois::Runtime::SendBuffer> b(numColumnHosts);
    static size_t empty_size = 0;
    auto columnBlockSize = numColumnBlocksPerHost * blockSize;
    std::vector<uint64_t> columnOffsets(numColumnHosts);
    columnOffsets[0] = 0;
    for (unsigned i = 1; i < numColumnHosts; ++i) {
      columnOffsets[i] = columnOffsets[i-1] + columnBlockSize;
    }
    auto& net = Galois::Runtime::getSystemNetworkInterface();
    uint32_t numNodesWithEdges = dummyOwnedNodes;

    auto ee = g.edge_begin(gid2host[base_hGraph::id].first);
    for (auto n = gid2host[base_hGraph::id].first; n < gid2host[base_hGraph::id].second; ++n) {
      uint32_t lsrc = 0;
      uint64_t cur = 0;
      if (isLocal(n)) {
        lsrc = G2L(n);
        cur = *graph.edge_begin(lsrc, Galois::MethodFlag::UNPROTECTED);
        if ((cur < (*graph.edge_end(lsrc))) && (lsrc < base_hGraph::numOwned)) {
          ++numNodesWithEdges;
        }
      }
      for (unsigned i = 0; i < numColumnHosts; ++i) {
        b[i].getVec().clear();
        Galois::Runtime::gSerialize(b[i], n);
      }
      empty_size = b[0].size();
      auto ii = ee;
      ee = g.edge_end(n);
      for (; ii < ee; ++ii) {
        uint64_t gdst = g.getEdgeDst(ii);
        int i;
        for (i = numColumnHosts - 1; i >= 0; --i) {
          if (gdst >= columnOffsets[i]) {
            break;
          }
        }
        if ((h_offset + i) == base_hGraph::id) {
          assert(isLocal(n));
          uint32_t ldst = G2L(gdst);
          loadEdge(graph, g, ii, ldst, cur);
        } else {
          Galois::Runtime::gSerialize(b[i], gdst);
          serializeEdgeData<GraphTy>(b[i], g, ii);
        }
      }
      for (unsigned i = 0; i < numColumnHosts; ++i) {
        if (b[i].size() > empty_size) {
          net.sendTagged(h_offset + i, Galois::Runtime::evilPhase, b[i]);
        }
      }
      if (isLocal(n)) {
        assert(cur == (*graph.edge_end(lsrc)));
      }
    }
    net.flush();

    while (numNodesWithEdges < base_hGraph::numOwned) { // can be done in parallel with sends/file-reading
      decltype(net.recieveTagged(Galois::Runtime::evilPhase, nullptr)) p;
      do {
        net.handleReceives();
        p = net.recieveTagged(Galois::Runtime::evilPhase, nullptr);
      } while (!p);
      auto& rb = p->second;
      uint64_t n;
      Galois::Runtime::gDeserialize(rb, n);
      assert(isLocal(n));
      uint32_t lsrc = G2L(n);
      uint64_t cur = *graph.edge_begin(lsrc, Galois::MethodFlag::UNPROTECTED);
      uint64_t cur_end = *graph.edge_end(lsrc);
      while (cur < cur_end) {
        uint64_t gdst;
        Galois::Runtime::gDeserialize(rb, gdst);
        uint32_t ldst = G2L(gdst);
        deserializeEdge(graph, rb, ldst, cur);
      }
      ++numNodesWithEdges;
    }
    ++Galois::Runtime::evilPhase;
  }

  template<typename GraphTy, typename std::enable_if<!std::is_void<typename GraphTy::edge_data_type>::value>::type* = nullptr>
  void loadEdge(GraphTy& graph, Galois::Graph::OfflineGraph& g, 
      Galois::Graph::OfflineGraph::edge_iterator& ii, uint32_t& ldst, uint64_t& cur) {
    auto gdata = g.getEdgeData<typename GraphTy::edge_data_type>(ii);
    graph.constructEdge(cur++, ldst, gdata);
  }

  template<typename GraphTy, typename std::enable_if<std::is_void<typename GraphTy::edge_data_type>::value>::type* = nullptr>
  void loadEdge(GraphTy& graph, Galois::Graph::OfflineGraph& g, 
      Galois::Graph::OfflineGraph::edge_iterator& ii, uint32_t& ldst, uint64_t &cur) {
    graph.constructEdge(cur++, ldst);
  }

  template<typename GraphTy, typename std::enable_if<!std::is_void<typename GraphTy::edge_data_type>::value>::type* = nullptr>
  void serializeEdgeData(Galois::Runtime::SendBuffer& b, Galois::Graph::OfflineGraph& g, 
      Galois::Graph::OfflineGraph::edge_iterator& ii) {
    auto gdata = g.getEdgeData<typename GraphTy::edge_data_type>(ii);
    Galois::Runtime::gSerialize(b, gdata);
  }

  template<typename GraphTy, typename std::enable_if<std::is_void<typename GraphTy::edge_data_type>::value>::type* = nullptr>
  void serializeEdgeData(Galois::Runtime::SendBuffer& b, Galois::Graph::OfflineGraph& g, 
      Galois::Graph::OfflineGraph::edge_iterator& ii) {
  }

  template<typename GraphTy, typename std::enable_if<!std::is_void<typename GraphTy::edge_data_type>::value>::type* = nullptr>
  void deserializeEdge(GraphTy& graph, Galois::Runtime::RecvBuffer& b, 
      uint32_t& ldst, uint64_t& cur) {
    typename GraphTy::edge_data_type gdata;
    Galois::Runtime::gDeserialize(b, gdata);
    graph.constructEdge(cur++, ldst, gdata);
  }

  template<typename GraphTy, typename std::enable_if<std::is_void<typename GraphTy::edge_data_type>::value>::type* = nullptr>
  void deserializeEdge(GraphTy& graph, Galois::Runtime::RecvBuffer& b, 
      uint32_t& ldst, uint64_t& cur) {
    graph.constructEdge(cur++, ldst);
  }

  void fill_slaveNodes(std::vector<std::vector<size_t>>& slaveNodes){
    auto rowBlockSize = numRowBlocksPerHost * blockSize;
    uint64_t rowOffset = gridRowID() * rowBlockSize;
    for (unsigned i = 0; i < numRowBlocksPerHost; ++i) {
      uint64_t src = rowOffset + (i * blockSize);
      auto h = getHostID(src);
      if (h == base_hGraph::id) continue;
      slaveNodes[h].reserve(slaveNodes[h].size() + blockSize);
      auto src_end = src + blockSize;
      for (; src < src_end; ++src) {
        if (globalToLocalMap.find(src) != globalToLocalMap.end()) {
          slaveNodes[h].push_back(src);
        }
      }
    }
    auto columnBlockSize = numColumnBlocksPerHost * blockSize;
    uint64_t columnOffset = gridColumnID() * columnBlockSize;
    for (uint32_t i = 0; i < numColumnBlocksPerHost; ++i) {
      auto dst = columnOffset + (i * blockSize);
      auto dst_end = dst + blockSize;
      if (dst_end > base_hGraph::totalNodes) dst_end = base_hGraph::totalNodes;
      if ((dst_end <= rowOffset) || (dst >= (rowOffset + rowBlockSize))) {
        auto h = getHostID(dst);
        assert(h == getHostID(dst_end-1));
        assert(h != base_hGraph::id);
        slaveNodes[h].reserve(slaveNodes[h].size() + blockSize);
        for (; dst < dst_end; ++dst) {
          if (globalToLocalMap.find(dst) != globalToLocalMap.end()) {
            slaveNodes[h].push_back(dst);
          }
        }
      } else { // also a source
        // owned nodes
      }
    }
  }

  std::string getPartitionFileName(const std::string& filename, const std::string & basename, unsigned hostID, unsigned num_hosts){
    return filename;
  }

  bool is_vertex_cut() const{
    return true;
  }

  uint64_t get_local_total_nodes() const {
    return numNodes;
  }

};

