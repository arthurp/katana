/*
 * This file belongs to the Galois project, a C++ library for exploiting parallelism.
 * The code is being released under the terms of the 3-Clause BSD License (a
 * copy is located in LICENSE.txt at the top-level directory).
 *
 * Copyright (C) 2018, The University of Texas at Austin. All rights reserved.
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
 */

/**
 * @file TBD
 *
 * Generic, new
 */

#ifndef _GALOIS_DIST_NEWGENERIC_H
#define _GALOIS_DIST_NEWGENERIC_H

#include "galois/graphs/DistributedGraph.h"
#include "galois/DReducible.h"
#include <sstream>

namespace galois {
namespace graphs {
/**
 * @tparam NodeTy type of node data for the graph
 * @tparam EdgeTy type of edge data for the graph
 *
 * @todo fully document and clean up code
 * @warning not meant for public use + not fully documented yet
 */
template <typename NodeTy, typename EdgeTy, typename Partitioner>
class NewDistGraphGeneric : public DistGraph<NodeTy, EdgeTy> {
  constexpr static const char* const GRNAME = "dGraph_Generic";
  Partitioner* graphPartitioner;

 public:
  //! typedef for base DistGraph class
  using base_DistGraph = DistGraph<NodeTy, EdgeTy>;

  //! GID = localToGlobalVector[LID]
  std::vector<uint64_t> localToGlobalVector;
  //! LID = globalToLocalMap[GID]
  std::unordered_map<uint64_t, uint32_t> globalToLocalMap;

  uint32_t numNodes;
  uint64_t numEdges;
  uint32_t nodesToReceive;

  /**
   * Free memory of a vector by swapping an empty vector with it
   */
  template<typename V>
  void freeVector(V& vectorToKill) {
    V dummyVector;
    vectorToKill.swap(dummyVector);
  }

  /**
   * get reader of a particular node
   */
  unsigned getHostReader(uint64_t gid) const {
    for (auto i = 0U; i < base_DistGraph::numHosts; ++i) {
      uint64_t start, end;
      std::tie(start, end) = base_DistGraph::gid2host[i];
      if (gid >= start && gid < end) {
        return i;
      }
    }

    return -1;
  }

  unsigned getHostID(uint64_t gid) const {
    assert(gid < base_DistGraph::numGlobalNodes);
    return graphPartitioner->getMaster(gid);
  }

  bool isOwned(uint64_t gid) const {
    assert(gid < base_DistGraph::numGlobalNodes);
    return (graphPartitioner->getMaster(gid) == base_DistGraph::id);
  }

  virtual bool isLocal(uint64_t gid) const {
    assert(gid < base_DistGraph::numGlobalNodes);
    return (globalToLocalMap.find(gid) != globalToLocalMap.end());
  }

  virtual uint32_t G2L(uint64_t gid) const {
    assert(isLocal(gid));
    return globalToLocalMap.at(gid);
  }

  uint32_t G2LEdgeCut(uint64_t gid, uint32_t globalOffset) const {
    assert(isLocal(gid));
    // optimized for edge cuts
    if (gid >= globalOffset && gid < globalOffset + base_DistGraph::numOwned)
      return gid - globalOffset;

    return globalToLocalMap.at(gid);
  }


  virtual uint64_t L2G(uint32_t lid) const {
    return localToGlobalVector[lid];
  }

  virtual bool nothingToSend(unsigned host,
                             typename base_DistGraph::SyncType syncType,
                             WriteLocation writeLocation,
                             ReadLocation readLocation) {
    auto& sharedNodes = (syncType == base_DistGraph::syncReduce) ?
                        base_DistGraph::mirrorNodes :
                        base_DistGraph::masterNodes;

    unsigned map = 2;
    if (syncType == base_DistGraph::syncReduce) {
      map = 0;
    } else {
      map = 1;
    }

    if (graphPartitioner->isCartCut()) {
      if (sharedNodes[host].size() > 0) {
        return graphPartitioner->isNotCommunicationPartner(host, map,
                                 writeLocation, readLocation,
                                 base_DistGraph::transposed);
      }
    } else {
      return (sharedNodes[host].size() == 0);
    }
    return true;
  }

  virtual bool nothingToRecv(unsigned host,
                             typename base_DistGraph::SyncType syncType,
                             WriteLocation writeLocation,
                             ReadLocation readLocation) {
    auto& sharedNodes = (syncType == base_DistGraph::syncReduce) ?
                        base_DistGraph::masterNodes :
                        base_DistGraph::mirrorNodes;

    unsigned map = 2;
    if (syncType == base_DistGraph::syncReduce) {
      map = 0;
    } else {
      map = 1;
    }

    if (graphPartitioner->isCartCut()) {
      if (sharedNodes[host].size() > 0) {
        return graphPartitioner->isNotCommunicationPartner(host, map,
                                 writeLocation, readLocation,
                                 base_DistGraph::transposed);
      }
    } else {
      return (sharedNodes[host].size() == 0);
    }
    return true;
  }

  /**
   * Constructor
   */
  NewDistGraphGeneric(const std::string& filename, unsigned host,
                   unsigned _numHosts, bool transpose = false,
                   bool readFromFile = false,
                   std::string localGraphFileName = "local_graph")
      : base_DistGraph(host, _numHosts) {
    galois::runtime::reportParam("dGraph", "GenericPartitioner", "0");
    galois::CondStatTimer<MORE_DIST_STATS> Tgraph_construct(
        "GraphPartitioningTime", GRNAME);
    Tgraph_construct.start();

    if (readFromFile) {
      galois::gPrint("[", base_DistGraph::id,
                     "] Reading local graph from file ",
                     localGraphFileName, "\n");
      base_DistGraph::read_local_graph_from_file(localGraphFileName);
      Tgraph_construct.stop();
      return;
    }

    galois::graphs::OfflineGraph g(filename);
    base_DistGraph::numGlobalNodes = g.size();
    base_DistGraph::numGlobalEdges = g.sizeEdges();
    std::vector<unsigned> dummy;
    // not actually getting masters, but getting assigned readers for nodes
    base_DistGraph::computeMasters(g, dummy);

    graphPartitioner = new Partitioner(host, _numHosts,
                                       base_DistGraph::numGlobalNodes,
                                       base_DistGraph::numGlobalEdges);
    // TODO abstract this away somehow
    graphPartitioner->saveGIDToHost(base_DistGraph::gid2host);

    uint64_t nodeBegin = base_DistGraph::gid2host[base_DistGraph::id].first;
    typename galois::graphs::OfflineGraph::edge_iterator edgeBegin =
        g.edge_begin(nodeBegin);
    uint64_t nodeEnd = base_DistGraph::gid2host[base_DistGraph::id].second;
    typename galois::graphs::OfflineGraph::edge_iterator edgeEnd =
        g.edge_begin(nodeEnd);

    // signifies how many outgoing edges a particular host should expect from
    // this host
    std::vector<std::vector<uint64_t>> numOutgoingEdges;
    // signifies if a host should create a node because it has an incoming edge
    std::vector<galois::DynamicBitSet> hasIncomingEdge;

    // only need to use for things that need communication
    if (!graphPartitioner->noCommunication()) {
      numOutgoingEdges.resize(base_DistGraph::numHosts);
      hasIncomingEdge.resize(base_DistGraph::numHosts);
    }

    // phase 0
    galois::graphs::BufferedGraph<EdgeTy> bufGraph;
    bufGraph.resetReadCounters();
    galois::StatTimer graphReadTimer("GraphReading", GRNAME);
    graphReadTimer.start();
    bufGraph.loadPartialGraph(filename, nodeBegin, nodeEnd, *edgeBegin,
                              *edgeEnd, base_DistGraph::numGlobalNodes,
                              base_DistGraph::numGlobalEdges);
    graphReadTimer.stop();

    // loop over all nodes, determine where neighbors are, assign masters
    galois::StatTimer phase0Timer("Phase0", GRNAME);
    phase0Timer.start();
    phase0(bufGraph);
    phase0Timer.stop();

    galois::StatTimer inspectionTimer("EdgeInspection", GRNAME);
    inspectionTimer.start();
    bufGraph.resetReadCounters();
    galois::gstl::Vector<uint64_t> prefixSumOfEdges;

    // assign edges to other nodes
    if (!graphPartitioner->noCommunication()) {
      edgeInspection(bufGraph, numOutgoingEdges, hasIncomingEdge,
                     inspectionTimer);
      galois::DynamicBitSet& finalIncoming = hasIncomingEdge[base_DistGraph::id];

      galois::StatTimer mapTimer("NodeMapping", GRNAME);
      mapTimer.start();
      nodeMapping(numOutgoingEdges, finalIncoming, prefixSumOfEdges);
      mapTimer.stop();

      finalIncoming.resize(0);
    } else {
      base_DistGraph::numOwned = nodeEnd - nodeBegin;
      uint64_t edgeOffset = *bufGraph.edgeBegin(nodeBegin);
      // edge prefix sum, no comm required
      edgeCutInspection(bufGraph, inspectionTimer, edgeOffset,
                        prefixSumOfEdges);
    }
    // inspection timer is stopped in edgeInspection function

    // flip partitioners that have a master assignment phase to stage 2
    // (meaning all nodes and masters that will be on this host are present in
    // the partitioner's metadata)
    graphPartitioner->enterStage2();

    // get memory back from inspection metadata
    numOutgoingEdges.clear();
    hasIncomingEdge.clear();
    // doubly make sure the data is cleared
    freeVector(numOutgoingEdges); // should no longer use this variable
    freeVector(hasIncomingEdge); // should no longer use this variable

    // Graph construction related calls

    base_DistGraph::beginMaster = 0;
    // Allocate and construct the graph
    base_DistGraph::graph.allocateFrom(numNodes, numEdges);
    base_DistGraph::graph.constructNodes();

    // edge end fixing
    auto& base_graph = base_DistGraph::graph;
    galois::do_all(
      galois::iterate((uint32_t)0, numNodes),
      [&](auto n) { base_graph.fixEndEdge(n, prefixSumOfEdges[n]); },
#if MORE_DIST_STATS
      galois::loopname("FixEndEdgeLoop"),
#endif
      galois::no_stats()
    );
    // get memory from prefix sum back
    prefixSumOfEdges.clear();
    freeVector(prefixSumOfEdges); // should no longer use this variable
    fillMirrors();

    base_DistGraph::printStatistics();

    // Edge loading
    if (!graphPartitioner->noCommunication()) {
      loadEdges(base_DistGraph::graph, bufGraph);
    } else {
      // Edge cut construction
      edgeCutLoad(base_DistGraph::graph, bufGraph);
      bufGraph.resetAndFree();
    }

    // Finalization

    // TODO this is a hack; fix it somehow
    if (graphPartitioner->isVertexCut() && !graphPartitioner->isCartCut()) {
      base_DistGraph::numNodesWithEdges = numNodes;
    }

    if (transpose && (numNodes > 0)) {
      // consider all nodes to have outgoing edges (TODO better way to do this?)
      base_DistGraph::numNodesWithEdges = numNodes;
      base_DistGraph::graph.transpose(GRNAME);
      base_DistGraph::transposed = true;
    }

    galois::CondStatTimer<MORE_DIST_STATS> Tthread_ranges("ThreadRangesTime",
                                                          GRNAME);

    Tthread_ranges.start();
    base_DistGraph::determineThreadRanges();
    Tthread_ranges.stop();

    base_DistGraph::determineThreadRangesMaster();
    base_DistGraph::determineThreadRangesWithEdges();
    base_DistGraph::initializeSpecificRanges();

    Tgraph_construct.stop();
    galois::gPrint("[", base_DistGraph::id, "] Graph construction complete.\n");

    galois::CondStatTimer<MORE_DIST_STATS> Tgraph_construct_comm(
        "GraphCommSetupTime", GRNAME);

    Tgraph_construct_comm.start();
    base_DistGraph::setup_communication();
    Tgraph_construct_comm.stop();
  }

  /**
   * Free the graph partitioner
   */
  ~NewDistGraphGeneric() {
    delete graphPartitioner;
  }

 private:
  /**
   * For each other host, determine which nodes that this host needs to get
   * info from
   *
   * @param bufGraph Buffered graph used to loop over edges
   * @param neighborOnHosts Vector of bitsets (one for each host); at end
   * of execution, marked bits signify neighbors on this host that that other
   * host has read (and therefore must sync with me)
   */
  // steps 1 and 2 of neighbor location setup: memory allocation, bitset setting
  void phase0BitsetSetup(
    galois::graphs::BufferedGraph<EdgeTy>& bufGraph,
    galois::gstl::Vector<galois::DynamicBitSet>& neighborOnHosts
  ) {
    // Step 1: setup bitsets
    galois::do_all(
      galois::iterate(0u, (unsigned)base_DistGraph::numHosts),
      [&] (unsigned h) {
        // get number of nodes on host h
        uint64_t startNode;
        uint64_t endNode;
        std::tie(startNode, endNode) = base_DistGraph::gid2host[h];
        // setup bitset
        neighborOnHosts[h].resize(endNode - startNode);
        neighborOnHosts[h].reset();
      },
      #if MORE_DIST_STATS
      galois::loopname("SetupNeighborHostBitsets"),
      #endif
      galois::steal(),
      galois::no_stats()
    );

    // Step 2: loop over all local nodes, determine neighbor locations
    galois::do_all(
      galois::iterate(base_DistGraph::gid2host[base_DistGraph::id].first,
                      base_DistGraph::gid2host[base_DistGraph::id].second),
      [&] (unsigned n) {
        auto ii = bufGraph.edgeBegin(n);
        auto ee = bufGraph.edgeEnd(n);
        for (; ii < ee; ++ii) {
          uint32_t dst = bufGraph.edgeDestination(*ii);
          unsigned hostLocation = getHostReader(dst);
          // set on bitset
          neighborOnHosts[getHostReader(dst)].set(
            dst - base_DistGraph::gid2host[hostLocation].first
          );
        }
      },
      #if MORE_DIST_STATS
      galois::loopname("DetermineNeighborLocations"),
      #endif
      galois::steal(),
      galois::no_stats()
    );
  }

  // sets up the gid to lid mapping for phase 0
  /**
   * Set up the GID to LID mapping for phase 0: In the mapping vector,
   * read nodes occupy the first chunk, and nodes read by other hosts follow.
   *
   * @param neighborOnHost
   * @param gid2Offsets mapping vector: element at an offset corresponds to a
   * particular GID (and its master)
   * @returns Number of set bits over all bitsets
   */
  uint64_t phase0MapSetup(
    galois::gstl::Vector<galois::DynamicBitSet>& neighborOnHosts,
    std::map<uint64_t, uint32_t>& gid2Offsets
  ) {
    uint64_t curCount = 0;
    uint64_t numLocal = base_DistGraph::gid2host[base_DistGraph::id].second -
                        base_DistGraph::gid2host[base_DistGraph::id].first;

    for (unsigned h = 0; h < base_DistGraph::numHosts; h++) {
      if (h != base_DistGraph::id) {
        uint64_t hostOffset = base_DistGraph::gid2host[h].first;
        // get set bits in the bitset
        std::vector<uint32_t> setOffsets = neighborOnHosts[h].getOffsets();

        // map the gid to a local id
        for (uint32_t i : setOffsets) {
          gid2Offsets[i + hostOffset] = numLocal + curCount;
          //galois::gDebug("[", base_DistGraph::id, "] ", i + hostOffset, " map to"
          //               " offset ", numLocal +   curCount);
          curCount++;
        }
      }
    }

    return curCount;
  }

  // steps 4 and 5 of neighbor location setup
  /**
   * Let other hosts know which nodes they need to send to me by giving them
   * the bitset marked with nodes I am interested in on the other host.
   *
   * @param neighborOnHosts Bitsets that signify which nodes from a host are
   * important to this host (me).
   */
  void phase0SendRecv(
    galois::gstl::Vector<galois::DynamicBitSet>& neighborOnHosts
  ) {
    auto& net = galois::runtime::getSystemNetworkInterface();

    // Step 4: send bitset to other hosts
    for (unsigned h = 0; h < base_DistGraph::numHosts; h++) {
      galois::runtime::SendBuffer bitsetBuffer;

      if (h != base_DistGraph::id) {
        galois::runtime::gSerialize(bitsetBuffer, neighborOnHosts[h]);
        net.sendTagged(h, galois::runtime::evilPhase, bitsetBuffer);
      }
    }

    // Step 5: recv bitset to other hosts; this indicates which local nodes each
    // other host needs to be informed of updates of
    for (unsigned h = 0; h < net.Num - 1; h++) {
      decltype(net.recieveTagged(galois::runtime::evilPhase, nullptr)) p;
      do {
        p = net.recieveTagged(galois::runtime::evilPhase, nullptr);
      } while (!p);
      uint32_t sendingHost = p->first;
      // deserialize into neighbor bitsets
      galois::runtime::gDeserialize(p->second, neighborOnHosts[sendingHost]);

      //for (uint32_t i : neighborOnHosts[sendingHost].getOffsets()) {
      //  galois::gDebug("[", base_DistGraph::id, "] ", i, " is set");
      //}
    }


    // comm phase complete
    base_DistGraph::increment_evilPhase();
  }

  /**
   * Given a set of loads in a vector and the accumulation to those loads,
   * synchronize them across hosts and do the accumulation into the vector
   * of loads.
   *
   * @param loads Vector of loads to accumulate to
   * @param accums Vector of accuulations to loads that occured since last
   * sync
   */
  void syncLoad(std::vector<uint64_t>& loads,
                std::vector<galois::CopyableAtomic<uint64_t>>& accums) {
    assert(loads.size() == accums.size());
    galois::DGAccumulator<uint64_t> syncer;
    // sync accum for each host one by one
    for (unsigned i = 0; i < loads.size(); i++) {
      syncer.reset();
      syncer += (accums[i].load());
      accums[i].store(0);
      uint64_t accumulation = syncer.reduce();
      loads[i] += accumulation;
    }
  }

  /**
   * Debug function: simply prints loads and accumulations
   *
   * @param loads Vector of loads to accumulate to
   * @param accums Vector of accuulations to loads that occured since last
   * sync
   */
  void printLoad(std::vector<uint64_t>& loads,
                 std::vector<galois::CopyableAtomic<uint64_t>>& accums) {
    assert(loads.size() == accums.size());
    for (unsigned i = 0; i < loads.size(); i++) {
      galois::gDebug("[", base_DistGraph::id, "] ", i, " total ", loads[i],
                     " accum ", accums[i].load());
    }
  }

  /**
   * Given a vector of data and a bitset specifying which elements in the data
   * vector need to be extracted, extract the appropriate elements into
   * a vector.
   *
   * @param offsets Bitset specifying which elements in the data vector need
   * to be extracted.
   * @param dataVector Data vector to extract data from according to the bitset
   * @return Vector of extracted elements
   */
  template <typename T>
  std::vector<T> getDataFromOffsets(galois::DynamicBitSet& offsets,
                                    const std::vector<T>& dataVector) {
    std::vector<T> toReturn;
    toReturn.resize(offsets.count());
    std::vector<uint32_t> offsetVector = offsets.getOffsets();
    assert(offsetVector.size() == toReturn.size());

    galois::do_all(
      galois::iterate((size_t)0, offsetVector.size()),
      [&] (unsigned i) {
        toReturn[i] = dataVector[offsetVector[i]];
      },
      galois::no_stats()
    );

    return toReturn;
  }


  /**
   * Given a host, a bitset that marks offsets, and a vector,
   * send the data located at the offsets from the vector to the
   * specified host. If bitset is unmarked, send a no-op.
   *
   * @param targetHost Host to send data to
   * @param toSync Bitset that specifies which offsets in the data vector
   * to send
   * @param dataVector Data to be sent to the target host
   */
  void sendOffsets(unsigned targetHost, galois::DynamicBitSet& toSync,
                   std::vector<uint32_t>& dataVector) {
    auto& net = galois::runtime::getSystemNetworkInterface();

    // this means there are updates to send
    if (toSync.count()) {
      // get masters to send into a vector
      std::vector<uint32_t> mastersToSend = getDataFromOffsets(toSync,
                                                               dataVector);

      //for (unsigned i : mastersToSend) {
      //  galois::gDebug("[", base_DistGraph::id, "] gid ",
      //                 i + base_DistGraph::gid2host[net.ID].first,
      //                 " master send ", i);
      //}
      // assert it's a positive number
      assert(mastersToSend.size());

      size_t num_selected = toSync.count();
      size_t num_total = toSync.size();
      // figure out how to send (most efficient method; either bitset
      // and data or offsets + data)
      size_t bitset_alloc_size =
          ((num_total + 63) / 64) * sizeof(uint64_t) + (2 * sizeof(size_t));
      size_t bitsetDataSize = (num_selected * sizeof(uint32_t)) +
                              bitset_alloc_size + sizeof(num_selected);
      size_t offsetsDataSize = (num_selected * sizeof(uint32_t)) +
                               (num_selected * sizeof(unsigned int)) +
                               sizeof(uint32_t) + sizeof(num_selected);

      galois::runtime::SendBuffer b;

      // tag with send method and do send
      if (bitsetDataSize < offsetsDataSize) {
        // send bitset, tag 1
        galois::runtime::gSerialize(b, 1u);
        galois::runtime::gSerialize(b, toSync);
        galois::runtime::gSerialize(b, mastersToSend);
      } else {
        // send offsets, tag 2
        galois::runtime::gSerialize(b, 2u);
        galois::runtime::gSerialize(b, toSync.getOffsets());
        galois::runtime::gSerialize(b, mastersToSend);
      }
    } else {
      // send empty no-op message, tag 0
      galois::runtime::SendBuffer b;
      galois::runtime::gSerialize(b, 0u);
    }
  }

  /**
   * Send new master assignment updates to other hosts based on bitsets
   * for each hosts prepared in advance.
   *
   * @param newAssignedNodes Bitset specifying which nodes on this host have
   * been updated
   * @param localNodeToMaster Vector map: an offset corresponds to a particular
   * GID; indicates masters of GIDs
   * @param neighborOnHosts Specifies which of my local nodes need to a
   * sent to a particular master
   */
  void syncAssignmentSends(galois::DynamicBitSet& newAssignedNodes,
               std::vector<uint32_t>& localNodeToMaster,
               galois::gstl::Vector<galois::DynamicBitSet>& neighborOnHosts) {

    galois::DynamicBitSet toSync;
    toSync.resize(newAssignedNodes.size());

    // send loop
    for (unsigned h = 0; h < base_DistGraph::numHosts; h++) {
      if (h != base_DistGraph::id) {
        toSync.reset();
        assert(newAssignedNodes.size() == neighborOnHosts[h].size());

        // do bitwise and with updates, see if any new updates exist to send
        toSync.bitwise_and(newAssignedNodes, neighborOnHosts[h]);
        // do actual send based on sync bitset
        sendOffsets(h, toSync, localNodeToMaster);
      }
    }
  }

  /**
   * Receive offsets and masters into the provided vectors and return sending
   * host and the message type.
   *
   * @param receivedOffsets vector to receive offsets into
   * @param receivedMasters vector to receive masters mappings into
   * @returns sending host and message type of received data
   */
  std::pair<unsigned, unsigned> recvOffsetsAndMasters(
      std::vector<uint32_t>& receivedOffsets,
      std::vector<uint32_t>& receivedMasters
  ) {
    auto& net = galois::runtime::getSystemNetworkInterface();

    decltype(net.recieveTagged(galois::runtime::evilPhase, nullptr)) p;
    do {
      p = net.recieveTagged(galois::runtime::evilPhase, nullptr);
    } while (!p);

    uint32_t sendingHost = p->first;
    unsigned messageType = (unsigned)-1;

    // deserialize message type
    galois::runtime::gDeserialize(p->second, messageType);

    if (messageType == 1) {
      // bitset; deserialize, then get offsets
      galois::DynamicBitSet receivedSet;
      galois::runtime::gDeserialize(p->second, receivedSet);
      receivedOffsets = receivedSet.getOffsets();
      galois::runtime::gDeserialize(p->second, receivedMasters);
    } else if (messageType == 2) {
      // offsets
      galois::runtime::gDeserialize(p->second, receivedOffsets);
      galois::runtime::gDeserialize(p->second, receivedMasters);
    } else if (messageType != 0) {
      GALOIS_DIE("Invalid message type for sync of master assignments");
    }

    galois::gDebug("[", base_DistGraph::id, "] host ", sendingHost,
                   " send message type ", messageType);

    return std::make_pair(sendingHost, messageType);
  }

  /**
   * Receive new master assignment updates from other hosts and update local
   * mappings.
   *
   * @param localNodeToMaster Vector map: an offset corresponds to a particular
   * GID; indicates masters of GIDs
   * @param gid2offsets Map of GIDs to the offset into the vector map that
   * corresponds to it
   */
  void syncAssignmentReceives(std::vector<uint32_t>& localNodeToMaster,
                              std::map<uint64_t, uint32_t>& gid2offsets) {
    // receive loop
    for (unsigned h = 0; h < base_DistGraph::numHosts - 1; h++) {
      unsigned sendingHost;
      unsigned messageType;
      std::vector<uint32_t> receivedOffsets;
      std::vector<uint32_t> receivedMasters;

      std::tie(sendingHost, messageType) =
        recvOffsetsAndMasters(receivedOffsets, receivedMasters);

      uint64_t hostOffset = base_DistGraph::gid2host[sendingHost].first;
      galois::gDebug("[", base_DistGraph::id, "] host ", sendingHost,
                     " offset ", hostOffset);

      if (messageType == 1 || messageType == 2) {
        // if execution gets here, messageType was 1 or 2
        assert(receivedMasters.size() == receivedOffsets.size());

        galois::do_all(
          galois::iterate((size_t)0, receivedMasters.size()),
          [&] (size_t i) {
            uint64_t curGID = hostOffset + receivedOffsets[i];
            uint32_t indexIntoMap = gid2offsets[curGID];
            galois::gDebug("[", base_DistGraph::id, "] gid ", curGID,
                           " offset ", indexIntoMap);
            localNodeToMaster[indexIntoMap] = receivedMasters[i];
          },
          galois::no_stats()
        );
      }
      //for (uint32_t i : neighborOnHosts[sendingHost].getOffsets()) {
      //  galois::gDebug("[", base_DistGraph::id, "] ", i, " is set");
      //}
    }
  }

  /**
   * Send/receive new master assignment updates to other hosts.
   *
   * @param newAssignedNodes Bitset specifying which nodes on this host have
   * been updated
   * @param localNodeToMaster Vector map: an offset corresponds to a particular
   * GID; indicates masters of GIDs
   * @param neighborOnHosts Specifies which of my local nodes need to a
   * sent to a particular master
   * @param gid2offsets Map of GIDs to the offset into the vector map that
   * corresponds to it
   */
  void syncAssignment(galois::DynamicBitSet& newAssignedNodes,
      std::vector<uint32_t>& localNodeToMaster,
      galois::gstl::Vector<galois::DynamicBitSet>& neighborOnHosts,
      std::map<uint64_t, uint32_t>& gid2offsets) {
    syncAssignmentSends(newAssignedNodes, localNodeToMaster, neighborOnHosts);
    syncAssignmentReceives(localNodeToMaster, gid2offsets);
    newAssignedNodes.reset();
  }


  /**
   * Send masters mappings that were read on this host to their appropirate
   * owners
   *
   * @param mastersOnHosts Vector with info on which local nodes belong on which
   * masters
   * @param localNodeToMaster local id to master mapping map
   * @param neighborOnHosts bitsets specifying which hosts have which neighbors
   * that this host has read
   */
  void sendMastersToOwners(std::vector<galois::DynamicBitSet>& mastersOnHosts,
                 std::vector<uint32_t>& localNodeToMaster,
                 galois::gstl::Vector<galois::DynamicBitSet>& neighborOnHosts) {
    // for each host, determine which master assignments still need to be sent
    // (if a host is a master of a node, but that node is not present as a
    // neighbor on the host, then this host needs to send the master assignment)
    galois::DynamicBitSet toSend;
    toSend.resize(base_DistGraph::gid2host[base_DistGraph::id].second -
                  base_DistGraph::gid2host[base_DistGraph::id].first);

    for (unsigned h = 0; h < base_DistGraph::numHosts; ++h) {
      if (h != base_DistGraph::id) {
        toSend.reset();
        // send if set on "mastersOnHosts" but not set on neighborOnHosts:
        // xor with neighborOnHosts, then and on mastersOnHosts
        toSend.bitwise_xor(mastersOnHosts[h], neighborOnHosts[h]);
        toSend.bitwise_and(mastersOnHosts[h]);
        sendOffsets(h, toSend, localNodeToMaster);
      }
    }
  }

  /**
   * Receive master mapping messages from hosts and add it to the graph
   * partitioner's map.
   */
  void recvMastersToOwners() {
    for (unsigned h = 0; h < base_DistGraph::numHosts - 1; h++) {
      unsigned sendingHost;
      unsigned messageType;
      std::vector<uint32_t> receivedOffsets;
      std::vector<uint32_t> receivedMasters;

      std::tie(sendingHost, messageType) =
        recvOffsetsAndMasters(receivedOffsets, receivedMasters);

      if (messageType == 1 || messageType == 2) {
        assert(receivedMasters.size() == receivedOffsets.size());
        uint64_t hostOffset = base_DistGraph::gid2host[sendingHost].first;

        // must be single threaded as map updating isn't thread-safe
        for (unsigned i = 0; i < receivedMasters.size(); i++) {
          uint64_t gidToMap = hostOffset + receivedOffsets[i];
          bool newMapped =
            graphPartitioner->addMasterMapping(gidToMap, receivedMasters[i]);
          assert(newMapped);
        }
      }
    }
  }

  /**
   * phase responsible for master assignment
   */
  void phase0(galois::graphs::BufferedGraph<EdgeTy>& bufGraph) {
    galois::gstl::Vector<galois::DynamicBitSet> neighborOnHosts;
    neighborOnHosts.resize(base_DistGraph::numHosts);

    // determine on which hosts that this host's read nodes havs neighbors on
    phase0BitsetSetup(bufGraph, neighborOnHosts);
    // gid to vector offset setup
    std::map<uint64_t, uint32_t> gid2offsets;
    uint64_t neighborCount = phase0MapSetup(neighborOnHosts, gid2offsets);
    galois::gDebug("[", base_DistGraph::id, "] num neighbors found is ",
                   neighborCount);
    // send off neighbor metadata
    phase0SendRecv(neighborOnHosts);

    // setup other partitioning metadata: nodes on each host, edges on each
    // host (as determined by edge cut)
    std::vector<uint64_t> nodeLoads;
    std::vector<uint64_t> edgeLoads;
    std::vector<galois::CopyableAtomic<uint64_t>> nodeAccum;
    std::vector<galois::CopyableAtomic<uint64_t>> edgeAccum;
    nodeLoads.assign(base_DistGraph::numHosts, 0);
    edgeLoads.assign(base_DistGraph::numHosts, 0);
    nodeAccum.assign(base_DistGraph::numHosts, 0);
    edgeAccum.assign(base_DistGraph::numHosts, 0);
    // this above all to be synchronized via DGAccumulators

    uint32_t numLocalNodes =
      base_DistGraph::gid2host[base_DistGraph::id].second -
      base_DistGraph::gid2host[base_DistGraph::id].first;

    std::vector<uint32_t> localNodeToMaster;
    localNodeToMaster.assign(numLocalNodes + neighborCount, (uint32_t)-1);
    uint64_t globalOffset = base_DistGraph::gid2host[base_DistGraph::id].first;

    // bitset setup for newly assigned nodes
    galois::DynamicBitSet newAssignedNodes;
    newAssignedNodes.resize(numLocalNodes);
    newAssignedNodes.reset();

    std::vector<galois::DynamicBitSet> mastersOnHosts;
    mastersOnHosts.resize(base_DistGraph::numHosts);
    galois::do_all(
      galois::iterate(0u, base_DistGraph::numHosts),
      [&] (unsigned h) {
        mastersOnHosts[h].resize(numLocalNodes);
        mastersOnHosts[h].reset();
      },
      galois::no_stats()
    );

    for (uint32_t i : localNodeToMaster) {
      assert(i == (uint32_t)-1);
    }

    for (unsigned syncRound = 0; syncRound < stateRounds; syncRound++) {
      uint32_t beginNode;
      uint32_t endNode;
      std::tie(beginNode, endNode) = galois::block_range(
        globalOffset, base_DistGraph::gid2host[base_DistGraph::id].second,
        syncRound, stateRounds);

      galois::do_all(
        // iterate over my read nodes
        galois::iterate(beginNode, endNode),
        [&] (uint32_t node) {
          // determine master function takes source node, iterator of
          // neighbors
          uint32_t assignedHost = graphPartitioner->determineMaster(node,
                                    bufGraph, localNodeToMaster, gid2offsets,
                                    nodeLoads, nodeAccum, edgeLoads, edgeAccum);
          // != -1 means it was assigned a host
          if (assignedHost != (uint32_t)-1) {
            // update mapping; this is a local node, so can get position
            // on map with subtraction
            localNodeToMaster[node - globalOffset] = assignedHost;
            // set update bitset
            newAssignedNodes.set(node - globalOffset);
            // set the appropriate bitset signifying which host has the node
            // as the master
            mastersOnHosts[assignedHost].set(node - globalOffset);
            //galois::gDebug("[", base_DistGraph::id, "] state round ", syncRound,
            //               " set ", node, " ", node - globalOffset);
          }
        },
        #if MORE_DIST_STATS
        galois::loopname("DetermineMasters"),
        #endif
        galois::steal(),
        galois::no_stats()
      );

      // do synchronization of master assignment of neighbors
      syncAssignment(newAssignedNodes, localNodeToMaster, neighborOnHosts,
                     gid2offsets);

      // debug build prints
      //printLoad(nodeLoads, nodeAccum);
      //printLoad(edgeLoads, edgeAccum);

      // sync node/edge loads
      syncLoad(nodeLoads, nodeAccum);
      syncLoad(edgeLoads, edgeAccum);
    }

    // sanity check for correctness (all should be assigned)
    for (uint32_t i = 0; i < localNodeToMaster.size(); i++) {
      if (localNodeToMaster[i] == (uint32_t)-1) {
        //galois::gDebug("[", base_DistGraph::id, "] bad index ", i);
        assert(localNodeToMaster[i] != (uint32_t)-1);
      }
    }
    base_DistGraph::increment_evilPhase();

    // TODO one more step: let masters know of nodes they own (if they don't
    // have the node locally then this is the only way they will learn about
    // it)
    sendMastersToOwners(mastersOnHosts, localNodeToMaster, neighborOnHosts);
    recvMastersToOwners();

    base_DistGraph::increment_evilPhase();

    graphPartitioner->saveGID2HostInfo(gid2offsets, localNodeToMaster,
                                       bufGraph.getNodeOffset());
  }

  void edgeCutInspection(galois::graphs::BufferedGraph<EdgeTy>& bufGraph,
                         galois::StatTimer& inspectionTimer,
                         uint64_t edgeOffset,
                         galois::gstl::Vector<uint64_t>& prefixSumOfEdges) {
    galois::DynamicBitSet incomingMirrors;
    incomingMirrors.resize(base_DistGraph::numGlobalNodes);
    incomingMirrors.reset();
    uint32_t myID = base_DistGraph::id;
    uint64_t globalOffset = base_DistGraph::gid2host[base_DistGraph::id].first;

    // already set before this is called
    localToGlobalVector.resize(base_DistGraph::numOwned);
    prefixSumOfEdges.resize(base_DistGraph::numOwned);

    galois::do_all(
      galois::iterate(base_DistGraph::gid2host[base_DistGraph::id].first,
                      base_DistGraph::gid2host[base_DistGraph::id].second),
      [&] (auto n) {
        auto ii = bufGraph.edgeBegin(n);
        auto ee = bufGraph.edgeEnd(n);
        for (; ii < ee; ++ii) {
          uint32_t dst = bufGraph.edgeDestination(*ii);
          if (graphPartitioner->getMaster(dst) != myID) {
            incomingMirrors.set(dst);
          }
        }
        prefixSumOfEdges[n - globalOffset] = (*ee) - edgeOffset;
        localToGlobalVector[n - globalOffset] = n;
      },
      #if MORE_DIST_STATS
      galois::loopname("EdgeInspectionLoop"),
      #endif
      galois::steal(),
      galois::no_stats()
    );
    inspectionTimer.stop();

    uint64_t allBytesRead = bufGraph.getBytesRead();
    galois::gPrint(
        "[", base_DistGraph::id,
        "] Edge inspection time: ", inspectionTimer.get_usec() / 1000000.0f,
        " seconds to read ", allBytesRead, " bytes (",
        allBytesRead / (float)inspectionTimer.get_usec(), " MBPS)\n");

    // get incoming mirrors ready for creation
    uint32_t additionalMirrorCount = incomingMirrors.count();
    localToGlobalVector.resize(localToGlobalVector.size() +
                               additionalMirrorCount);
    if (base_DistGraph::numOwned > 0) {
      // fill prefix sum with last number (incomings have no edges)
      prefixSumOfEdges.resize(prefixSumOfEdges.size() + additionalMirrorCount,
                              prefixSumOfEdges.back());
    } else {
      prefixSumOfEdges.resize(additionalMirrorCount);
    }

    if (additionalMirrorCount > 0) {
      // TODO move this part below into separate function
      uint32_t totalNumNodes = base_DistGraph::numGlobalNodes;
      uint32_t activeThreads = galois::getActiveThreads();
      std::vector<uint64_t> threadPrefixSums(activeThreads);
      galois::on_each(
        [&](unsigned tid, unsigned nthreads) {
          size_t beginNode;
          size_t endNode;
          std::tie(beginNode, endNode) = galois::block_range(0u,
                                           totalNumNodes, tid, nthreads);
          uint64_t count = 0;
          for (size_t i = beginNode; i < endNode; i++) {
            if (incomingMirrors.test(i)) ++count;
          }
          threadPrefixSums[tid] = count;
        }
      );
      // get prefix sums
      for (unsigned int i = 1; i < threadPrefixSums.size(); i++) {
        threadPrefixSums[i] += threadPrefixSums[i - 1];
      }

      assert(threadPrefixSums.back() == additionalMirrorCount);

      uint32_t startingNodeIndex = base_DistGraph::numOwned;
      // do actual work, second on_each
      galois::on_each(
        [&] (unsigned tid, unsigned nthreads) {
          size_t beginNode;
          size_t endNode;
          std::tie(beginNode, endNode) = galois::block_range(0u,
                                           totalNumNodes, tid, nthreads);
          // start location to start adding things into prefix sums/vectors
          uint32_t threadStartLocation = 0;
          if (tid != 0) {
            threadStartLocation = threadPrefixSums[tid - 1];
          }
          uint32_t handledNodes = 0;
          for (size_t i = beginNode; i < endNode; i++) {
            if (incomingMirrors.test(i)) {
              localToGlobalVector[startingNodeIndex + threadStartLocation +
                                  handledNodes] = i;
              handledNodes++;
            }
          }
        }
      );
    }

    numNodes = base_DistGraph::numOwned + additionalMirrorCount;
    if (prefixSumOfEdges.size() != 0) {
      numEdges = prefixSumOfEdges.back();
    } else {
      numEdges = 0;
    }
    assert(localToGlobalVector.size() == numNodes);
    assert(prefixSumOfEdges.size() == numNodes);

    // g2l mapping
    globalToLocalMap.reserve(numNodes);
    for (unsigned i = 0; i < numNodes; i++) {
      // global to local map construction
      globalToLocalMap[localToGlobalVector[i]] = i;
    }
    assert(globalToLocalMap.size() == numNodes);

    base_DistGraph::numNodesWithEdges = base_DistGraph::numOwned;
  }

  /**
   * Given a loaded graph, construct the edges in the DistGraph graph.
   * Variant that constructs edge data as well.
   *
   * @tparam GraphTy type of graph to construct
   *
   * @param [in,out] graph Graph to construct edges in
   * @param bGraph Buffered graph that has edges to write into graph in memory
   */
  template <typename GraphTy,
            typename std::enable_if<!std::is_void<
                typename GraphTy::edge_data_type>::value>::type* = nullptr>
  void edgeCutLoad(GraphTy& graph,
                 galois::graphs::BufferedGraph<EdgeTy>& bGraph) {
    if (base_DistGraph::id == 0) {
      galois::gPrint("Loading edge-data while creating edges\n");
    }

    uint64_t globalOffset = base_DistGraph::gid2host[base_DistGraph::id].first;
    bGraph.resetReadCounters();
    galois::StatTimer timer("EdgeLoading", GRNAME);
    timer.start();

    galois::do_all(
        galois::iterate(base_DistGraph::gid2host[base_DistGraph::id].first,
                        base_DistGraph::gid2host[base_DistGraph::id].second),
        [&](auto n) {
          auto ii       = bGraph.edgeBegin(n);
          auto ee       = bGraph.edgeEnd(n);
          uint32_t lsrc = this->G2LEdgeCut(n, globalOffset);
          uint64_t cur =
              *graph.edge_begin(lsrc, galois::MethodFlag::UNPROTECTED);
          for (; ii < ee; ++ii) {
            auto gdst           = bGraph.edgeDestination(*ii);
            decltype(gdst) ldst = this->G2LEdgeCut(gdst, globalOffset);
            auto gdata          = bGraph.edgeData(*ii);
            graph.constructEdge(cur++, ldst, gdata);
          }
          assert(cur == (*graph.edge_end(lsrc)));
        },
        #if MORE_DIST_STATS
        galois::loopname("EdgeLoadingLoop"),
        #endif
        galois::steal(),
        galois::no_stats());

    timer.stop();
    galois::gPrint("[", base_DistGraph::id,
                   "] Edge loading time: ", timer.get_usec() / 1000000.0f,
                   " seconds to read ", bGraph.getBytesRead(), " bytes (",
                   bGraph.getBytesRead() / (float)timer.get_usec(), " MBPS)\n");
  }

  /**
   * Given a loaded graph, construct the edges in the DistGraph graph.
   * No edge data.
   *
   * @tparam GraphTy type of graph to construct
   *
   * @param [in,out] graph Graph to construct edges in
   * @param bGraph Buffered graph that has edges to write into graph in memory
   */
  template <typename GraphTy,
            typename std::enable_if<std::is_void<
                typename GraphTy::edge_data_type>::value>::type* = nullptr>
  void edgeCutLoad(GraphTy& graph,
                   galois::graphs::BufferedGraph<EdgeTy>& bGraph) {
    if (base_DistGraph::id == 0) {
      galois::gPrint("Loading edge-data while creating edges\n");
    }

    uint64_t globalOffset = base_DistGraph::gid2host[base_DistGraph::id].first;
    bGraph.resetReadCounters();
    galois::StatTimer timer("EdgeLoading", GRNAME);
    timer.start();

    galois::do_all(
        galois::iterate(base_DistGraph::gid2host[base_DistGraph::id].first,
                        base_DistGraph::gid2host[base_DistGraph::id].second),
        [&](auto n) {
          auto ii       = bGraph.edgeBegin(n);
          auto ee       = bGraph.edgeEnd(n);
          uint32_t lsrc = this->G2LEdgeCut(n, globalOffset);
          uint64_t cur =
              *graph.edge_begin(lsrc, galois::MethodFlag::UNPROTECTED);
          for (; ii < ee; ++ii) {
            auto gdst           = bGraph.edgeDestination(*ii);
            decltype(gdst) ldst = this->G2LEdgeCut(gdst, globalOffset);
            graph.constructEdge(cur++, ldst);
          }
          assert(cur == (*graph.edge_end(lsrc)));
        },
        #if MORE_DIST_STATS
        galois::loopname("EdgeLoadingLoop"),
        #endif
        galois::steal(),
        galois::no_stats());

    timer.stop();
    galois::gPrint("[", base_DistGraph::id,
                   "] Edge loading time: ", timer.get_usec() / 1000000.0f,
                   " seconds to read ", bGraph.getBytesRead(), " bytes (",
                   bGraph.getBytesRead() / (float)timer.get_usec(), " MBPS)\n");
  }


  /**
   * Assign edges to hosts (but don't actually send), and send this information
   * out to all hosts
   * @param[in] bufGraph local graph to read
   * @param[in,out] numOutgoingEdges specifies which nodes on a host will have
   * outgoing edges
   * @param[in,out] hasIncomingEdge indicates which nodes (that need to be
   * created)on a host have incoming edges
   */
  void edgeInspection(galois::graphs::BufferedGraph<EdgeTy>& bufGraph,
                      std::vector<std::vector<uint64_t>>& numOutgoingEdges,
                      std::vector<galois::DynamicBitSet>& hasIncomingEdge,
                      galois::StatTimer& inspectionTimer) {
    // number of nodes that this host has read from disk
    uint32_t numRead = base_DistGraph::gid2host[base_DistGraph::id].second -
                       base_DistGraph::gid2host[base_DistGraph::id].first;

    // allocate space for outgoing edges
    for (uint32_t i = 0; i < base_DistGraph::numHosts; ++i) {
      numOutgoingEdges[i].assign(numRead, 0);
    }

    galois::DynamicBitSet hostHasOutgoing;
    hostHasOutgoing.resize(base_DistGraph::numHosts);
    hostHasOutgoing.reset();
    assignEdges(bufGraph, numOutgoingEdges, hasIncomingEdge, hostHasOutgoing);

    inspectionTimer.stop();
    // report edge inspection time
    uint64_t allBytesRead = bufGraph.getBytesRead();
    galois::gPrint(
        "[", base_DistGraph::id,
        "] Edge inspection time: ", inspectionTimer.get_usec() / 1000000.0f,
        " seconds to read ", allBytesRead, " bytes (",
        allBytesRead / (float)inspectionTimer.get_usec(), " MBPS)\n");

    sendInspectionData(numOutgoingEdges, hasIncomingEdge, hostHasOutgoing);

    // setup a single hasIncomingEdge bitvector

    uint32_t myHostID = base_DistGraph::id;
    if (hasIncomingEdge[myHostID].size() == 0) {
      hasIncomingEdge[myHostID].resize(base_DistGraph::numGlobalNodes);
      hasIncomingEdge[myHostID].reset();
    }
    recvInspectionData(numOutgoingEdges, hasIncomingEdge[myHostID]);
    base_DistGraph::increment_evilPhase();
  }

  /**
   * Inspect read edges and determine where to send them. Mark metadata as
   * necessary.
   *
   * @param[in] bufGraph local graph to read
   * @param[in,out] numOutgoingEdges specifies which nodes on a host will have
   * outgoing edges
   * @param[in,out] hasIncomingEdge indicates which nodes (that need to be
   * created)on a host have incoming edges
   * @param[in,out] hostHasOutgoing bitset tracking which hosts have outgoing
   * edges from this host
   */
  void assignEdges(galois::graphs::BufferedGraph<EdgeTy>& bufGraph,
                   std::vector<std::vector<uint64_t>>& numOutgoingEdges,
                   std::vector<galois::DynamicBitSet>& hasIncomingEdge,
                   galois::DynamicBitSet& hostHasOutgoing) {
    std::vector<galois::CopyableAtomic<char>>
      indicatorVars(base_DistGraph::numHosts);
    // initialize indicators of initialized bitsets to 0
    for (unsigned i = 0; i < base_DistGraph::numHosts; i++) {
      indicatorVars[i] = 0;
    }

    // global offset into my read nodes
    uint64_t globalOffset = base_DistGraph::gid2host[base_DistGraph::id].first;
    uint32_t globalNodes = base_DistGraph::numGlobalNodes;

    galois::do_all(
        // iterate over my read nodes
        galois::iterate(base_DistGraph::gid2host[base_DistGraph::id].first,
                        base_DistGraph::gid2host[base_DistGraph::id].second),
        [&](auto src) {
          auto ee        = bufGraph.edgeBegin(src);
          auto ee_end    = bufGraph.edgeEnd(src);
          uint64_t numEdgesL = std::distance(ee, ee_end);

          for (; ee != ee_end; ee++) {
            uint32_t dst = bufGraph.edgeDestination(*ee);
            uint32_t hostBelongs = -1;
            hostBelongs = graphPartitioner->getEdgeOwner(src, dst, numEdgesL);

            numOutgoingEdges[hostBelongs][src - globalOffset] += 1;
            hostHasOutgoing.set(hostBelongs);
            bool hostIsMasterOfDest =
              (hostBelongs == graphPartitioner->getMaster(dst));

            // this means a mirror must be created for destination node on
            // that host since it will not be created otherwise
            if (!hostIsMasterOfDest) {
              auto& bitsetStatus = indicatorVars[hostBelongs];

              // initialize the bitset if necessary
              if (bitsetStatus == 0) {
                char expected = 0;
                bool result = bitsetStatus.compare_exchange_strong(expected,
                                                                   1);
                // i swapped successfully, therefore do allocation
                if (result) {
                  hasIncomingEdge[hostBelongs].resize(globalNodes);
                  hasIncomingEdge[hostBelongs].reset();
                  bitsetStatus = 2;
                }
              }
              // until initialized, loop
              while (indicatorVars[hostBelongs] != 2);
              hasIncomingEdge[hostBelongs].set(dst);
            }
          }
        },
#if MORE_DIST_STATS
        galois::loopname("AssignEdges"),
#endif
        galois::steal(),
        galois::no_stats()
    );
  }

  /**
   * Given a vector specifying which nodes have edges for an unspecified
   * receiver host, save the masters of those nodes (which are known on this
   * host but not necessarily other hosts) into a vector and serialize it for
   * the receiver to update their master node mapping.
   *
   * @param b Send buffer
   * @param hostOutgoingEdges Number of edges that the receiver of this
   * vector should expect for each node on this host
   */
  void serializeOutgoingMasterMap(galois::runtime::SendBuffer& b,
                           const std::vector<uint64_t>& hostOutgoingEdges) {
    // 2 phase: one phase determines amount of work each thread does,
    // second has threads actually do copies
    uint32_t activeThreads = galois::getActiveThreads();
    std::vector<uint64_t> threadPrefixSums(activeThreads);
    size_t hostSize = base_DistGraph::gid2host[base_DistGraph::id].second -
                      base_DistGraph::gid2host[base_DistGraph::id].first;
    assert(hostSize == hostOutgoingEdges.size());

    // for each thread, figure out how many items it will work with
    // (non-zero outgoing edges)
    galois::on_each(
      [&](unsigned tid, unsigned nthreads) {
        size_t beginNode;
        size_t endNode;
        std::tie(beginNode, endNode) = galois::block_range((size_t)0, hostSize,
                                                           tid, nthreads);
        uint64_t count = 0;
        for (size_t i = beginNode; i < endNode; i++) {
          if (hostOutgoingEdges[i] > 0) {
            count++;
          }
        }
        threadPrefixSums[tid] = count;
      }
    );

    // get prefix sums
    for (unsigned int i = 1; i < threadPrefixSums.size(); i++) {
      threadPrefixSums[i] += threadPrefixSums[i - 1];
    }

    uint32_t numNonZero = threadPrefixSums[activeThreads - 1];
    std::vector<uint32_t> masterLocation;
    masterLocation.resize(numNonZero, (uint32_t)-1);
    // should only be in here if there's something to send in first place
    assert(numNonZero > 0);

    uint64_t startNode = base_DistGraph::gid2host[base_DistGraph::id].first;

    // do actual work, second on_each; find non-zeros again, get master
    // corresponding to that non-zero and send to other end
    galois::on_each(
      [&] (unsigned tid, unsigned nthreads) {
        size_t beginNode;
        size_t endNode;
        std::tie(beginNode, endNode) = galois::block_range((size_t)0, hostSize,
                                                           tid, nthreads);
        // start location to start adding things into prefix sums/vectors
        uint32_t threadStartLocation = 0;
        if (tid != 0) {
          threadStartLocation = threadPrefixSums[tid - 1];
        }

        uint32_t handledNodes = 0;
        for (size_t i = beginNode; i < endNode; i++) {
          if (hostOutgoingEdges[i] > 0) {
            // get master of i
            masterLocation[threadStartLocation + handledNodes] =
                graphPartitioner->getMaster(i + startNode);
            handledNodes++;
          }
        }
      }
    );

    for (uint32_t i : masterLocation) {
      assert(i != (uint32_t)-1);
    }

    // serialize into buffer; since this is sent along with vector receiver end
    // will know how to deal with it
    galois::runtime::gSerialize(b, masterLocation);
  }

  void serializeIncomingMasterMap(galois::runtime::SendBuffer& b,
                             const galois::DynamicBitSet& hostIncomingEdges) {
    size_t numOfNodes = hostIncomingEdges.count();
    std::vector<uint32_t> masterMap;
    masterMap.resize(numOfNodes, (uint32_t)-1);

    std::vector<uint32_t> bitsetOffsets = hostIncomingEdges.getOffsets();

    galois::do_all(
      galois::iterate((size_t)0, numOfNodes),
      [&] (size_t offset) {
        masterMap[offset] = graphPartitioner->getMaster(bitsetOffsets[offset]);
      },
      galois::no_stats()
    );

    for (uint32_t i : masterMap) {
      assert(i != (uint32_t)-1);
      assert(i >= 0 && i < base_DistGraph::numHosts);
    }

    // serialize into buffer; since this is sent along with vector receiver end
    // will know how to deal with it
    galois::runtime::gSerialize(b, masterMap);
  }

  void deserializeOutgoingMasterMap(uint32_t senderHost,
                          const std::vector<uint64_t>& hostOutgoingEdges,
                          const std::vector<uint32_t>& recvMasterLocations) {
    uint64_t hostOffset = base_DistGraph::gid2host[senderHost].first;
    size_t hostSize = base_DistGraph::gid2host[senderHost].second -
                      base_DistGraph::gid2host[senderHost].first;
    assert(hostSize == hostOutgoingEdges.size());
    galois::DynamicBitSet offsetsToConsider;
    offsetsToConsider.resize(hostSize);
    offsetsToConsider.reset();

    // step 1: figure out offsets that need to be handled (i.e. non-zero): only
    // handle if not already in map
    galois::do_all(
      galois::iterate((size_t)0, hostOutgoingEdges.size()),
      [&] (size_t offset) {
        if (hostOutgoingEdges[offset] > 0) {
          offsetsToConsider.set(offset);
        }
      },
      galois::no_stats(),
      galois::steal()
    );
    assert(offsetsToConsider.count() == recvMasterLocations.size());

    // step 2: using bitset that tells which offsets are set, add
    // to already master map in partitioner (this is single threaded
    // since map is not a concurrent data structure)
    size_t curCount = 0;
    //size_t actuallySet = 0;
    for (uint32_t offset : offsetsToConsider.getOffsets()) {
      //galois::gDebug("[", base_DistGraph::id, "] ", " setting ",
      //               offset + hostOffset, " from host ", senderHost,
      //               " to ", recvMasterLocations[curCount]);
      graphPartitioner->addMasterMapping(offset + hostOffset,
                                         recvMasterLocations[curCount]);
      //bool set = graphPartitioner->addMasterMapping(offset + hostOffset,
      //                                          recvMasterLocations[curCount]);
      //if (set) { actuallySet++; }
      curCount++;
    }

    //galois::gDebug("[", base_DistGraph::id, "] host ", senderHost, ": set ",
    //               actuallySet, " out of ", recvMasterLocations.size());
  }

  /**
   * Map GIDs to masters from incoming master map sent from hosts.
   *
   * @param senderHost host that sent the data
   * @param gids GIDs corresponding to the received master locations
   * @param recvMasterLocations masters of GIDs in the gids vector
   */
  void deserializeIncomingMasterMap(const std::vector<uint32_t>& gids,
                          const std::vector<uint32_t>& recvMasterLocations) {
    assert(gids.size() == recvMasterLocations.size());
    size_t curCount = 0;
    for (uint64_t gid : gids) {
      assert(gid < base_DistGraph::numGlobalNodes);
      //galois::gDebug("[", base_DistGraph::id, "] ", " in-setting ", gid, " to ",
      //               recvMasterLocations[curCount]);
      graphPartitioner->addMasterMapping(gid, recvMasterLocations[curCount]);
      curCount++;
    }
  }

  /**
   * Send data out from inspection to other hosts.
   *
   * @param[in,out] numOutgoingEdges specifies which nodes on a host will have
   * outgoing edges
   * @param[in,out] hasIncomingEdge indicates which nodes (that need to be
   * created)on a host have incoming edges
   * @param[in] hostHasOutgoing bitset tracking which hosts have outgoing
   * edges from this host
   */
  void sendInspectionData(std::vector<std::vector<uint64_t>>& numOutgoingEdges,
                          std::vector<galois::DynamicBitSet>& hasIncomingEdge,
                          galois::DynamicBitSet& hostHasOutgoing) {
    auto& net = galois::runtime::getSystemNetworkInterface();

    galois::GAccumulator<uint64_t> bytesSent;
    bytesSent.reset();

    for (unsigned h = 0; h < net.Num; h++) {
      if (h == net.ID) {
        // i have no outgoing edges i will keep; go ahead and clear
        if (!hostHasOutgoing.test(h)) {
          numOutgoingEdges[h].clear();
        }
        continue;
      }
      // send outgoing edges data off to comm partner
      galois::runtime::SendBuffer b;

      // only send if non-zeros exist
      if (hostHasOutgoing.test(h)) {
        galois::runtime::gSerialize(b, 1); // token saying data exists
        galois::runtime::gSerialize(b, numOutgoingEdges[h]);
        if (graphPartitioner->masterAssignPhase()) {
          serializeOutgoingMasterMap(b, numOutgoingEdges[h]);
        }
      } else {
        galois::runtime::gSerialize(b, 0); // token saying no data exists
      }
      numOutgoingEdges[h].clear();

      // determine form to send bitset in
      galois::DynamicBitSet& curBitset = hasIncomingEdge[h];
      uint64_t bitsetSize = curBitset.size(); // num bits
      uint64_t onlyOffsetsSize = curBitset.count() * 32;
      if (bitsetSize == 0) {
        // there was nothing there to send in first place
        galois::runtime::gSerialize(b, 0);
      } else if (onlyOffsetsSize <= bitsetSize) {
        // send only offsets
        std::vector<uint32_t> offsets = curBitset.getOffsets();
        galois::runtime::gSerialize(b, 2); // 2 = only offsets
        galois::runtime::gSerialize(b, offsets);

        if (graphPartitioner->masterAssignPhase()) {
          //galois::gDebug("incoming master map serialization");
          serializeIncomingMasterMap(b, curBitset);
        }
      } else {
        // send entire bitset
        galois::runtime::gSerialize(b, 1);
        galois::runtime::gSerialize(b, curBitset);
        if (graphPartitioner->masterAssignPhase()) {
          //galois::gDebug("incoming master map serialization");
          serializeIncomingMasterMap(b, curBitset);
        }
      }
      // get memory from bitset back
      curBitset.resize(0);

      bytesSent.update(b.size());

      // send buffer and free memory
      net.sendTagged(h, galois::runtime::evilPhase, b);
      b.getVec().clear();
    }

    galois::runtime::reportStat_Tsum(
      GRNAME, std::string("EdgeInspectionBytesSent"), bytesSent.reduce()
    );

    galois::gPrint("[", base_DistGraph::id, "] Inspection sends complete.\n");
  }

  /**
   * Receive data from inspection from other hosts. Processes the incoming
   * edge bitsets/offsets.
   *
   * @param[in,out] numOutgoingEdges specifies which nodes on a host will have
   * outgoing edges
   * @param[in,out] hasIncomingEdge indicates which nodes (that need to be
   * created) on this host have incoming edges
   */
  void recvInspectionData(std::vector<std::vector<uint64_t>>& numOutgoingEdges,
                          galois::DynamicBitSet& hasIncomingEdge) {
    auto& net = galois::runtime::getSystemNetworkInterface();

    for (unsigned h = 0; h < net.Num - 1; h++) {
      // expect data from comm partner back
      decltype(net.recieveTagged(galois::runtime::evilPhase, nullptr)) p;
      do {
        p = net.recieveTagged(galois::runtime::evilPhase, nullptr);
      } while (!p);

      uint32_t sendingHost = p->first;

      // get outgoing edges; first get status var
      uint32_t outgoingExists = 2;
      galois::runtime::gDeserialize(p->second, outgoingExists);

      if (outgoingExists == 1) {
        // actual data sent
        galois::runtime::gDeserialize(p->second, numOutgoingEdges[sendingHost]);

        if (graphPartitioner->masterAssignPhase()) {
          std::vector<uint32_t> recvMasterLocations;
          galois::runtime::gDeserialize(p->second, recvMasterLocations);
          deserializeOutgoingMasterMap(sendingHost,
                                       numOutgoingEdges[sendingHost],
                                       recvMasterLocations);
        }
      } else if (outgoingExists == 0) {
        // no data sent; just clear again
        numOutgoingEdges[sendingHost].clear();
      } else {
        GALOIS_DIE("invalid recv inspection data metadata mode, outgoing");
      }

      uint32_t bitsetMetaMode = 3; // initialize to invalid mode
      galois::runtime::gDeserialize(p->second, bitsetMetaMode);
      if (bitsetMetaMode == 1) {
        // sent as bitset; deserialize then or with main bitset
        galois::DynamicBitSet recvSet;
        galois::runtime::gDeserialize(p->second, recvSet);
        hasIncomingEdge.bitwise_or(recvSet);

        if (graphPartitioner->masterAssignPhase()) {
          std::vector<uint32_t> recvMasterLocations;
          galois::runtime::gDeserialize(p->second, recvMasterLocations);
          deserializeIncomingMasterMap(recvSet.getOffsets(),
                                       recvMasterLocations);
        }
      } else if (bitsetMetaMode == 2) {
        // sent as vector of offsets
        std::vector<uint32_t> recvOffsets;
        galois::runtime::gDeserialize(p->second, recvOffsets);
        for (uint32_t offset : recvOffsets) {
          hasIncomingEdge.set(offset);
        }

        if (graphPartitioner->masterAssignPhase()) {
          std::vector<uint32_t> recvMasterLocations;
          galois::runtime::gDeserialize(p->second, recvMasterLocations);
          deserializeIncomingMasterMap(recvOffsets, recvMasterLocations);
        }
      } else if (bitsetMetaMode == 0) {
        // do nothing; there was nothing to receive
      } else {
        GALOIS_DIE("invalid recv inspection data metadata mode");
      }
    }

    galois::gPrint("[", base_DistGraph::id,
                   "] Inspection receives complete.\n");
  }

  /**
   * Take inspection metadata and being mapping nodes/creating prefix sums,
   * return the prefix sum.
   */
  galois::gstl::Vector<uint64_t> nodeMapping(
    std::vector<std::vector<uint64_t>>& numOutgoingEdges,
    galois::DynamicBitSet& hasIncomingEdge,
    galois::gstl::Vector<uint64_t>& prefixSumOfEdges
  ) {
    numNodes = 0;
    numEdges = 0;
    nodesToReceive = 0;

    // reserve overestimation of nodes
    prefixSumOfEdges.reserve(base_DistGraph::numGlobalNodes /
                             base_DistGraph::numHosts * 1.15);
    localToGlobalVector.reserve(base_DistGraph::numGlobalNodes /
                                base_DistGraph::numHosts * 1.15);

    inspectMasterNodes(numOutgoingEdges, prefixSumOfEdges);
    inspectOutgoingNodes(numOutgoingEdges, prefixSumOfEdges);
    createIntermediateMetadata(prefixSumOfEdges, hasIncomingEdge.count());
    inspectIncomingNodes(hasIncomingEdge, prefixSumOfEdges);
    finalizeInspection(prefixSumOfEdges);

    galois::gDebug("[", base_DistGraph::id, "] To receive this many nodes: ",
                   nodesToReceive);

    galois::gPrint("[", base_DistGraph::id, "] Inspection mapping complete.\n");
    return prefixSumOfEdges;
  }

  /**
   * Inspect master nodes; loop over all nodes, determine if master; if is,
   * create mapping + get num edges
   */
  void inspectMasterNodes(
    std::vector<std::vector<uint64_t>>& numOutgoingEdges,
    galois::gstl::Vector<uint64_t>& prefixSumOfEdges
  ) {
    uint32_t myHID = base_DistGraph::id;

    galois::GAccumulator<uint32_t> toReceive;
    toReceive.reset();

    for (unsigned h = 0; h < base_DistGraph::numHosts; ++h) {
      uint32_t activeThreads = galois::getActiveThreads();
      std::vector<uint64_t> threadPrefixSums(activeThreads);
      uint64_t startNode = base_DistGraph::gid2host[h].first;
      uint64_t lastNode  = base_DistGraph::gid2host[h].second;
      size_t hostSize = lastNode - startNode;

      if (numOutgoingEdges[h].size() != 0) {
        assert(hostSize == numOutgoingEdges[h].size());
      }

      // for each thread, figure out how many items it will work with (only
      // owned nodes)
      galois::on_each(
        [&](unsigned tid, unsigned nthreads) {
          size_t beginNode;
          size_t endNode;
          // loop over all nodes that host h has read
          std::tie(beginNode, endNode) = galois::block_range((size_t)0,
                                             hostSize, tid, nthreads);
          uint64_t count = 0;
          for (size_t i = beginNode; i < endNode; i++) {
            //galois::gDebug("[", base_DistGraph::id, "] ", i + startNode,
            //               " mapped to ",
            //               graphPartitioner->getMaster(i+startNode));
            if (graphPartitioner->getMaster(i + startNode) == myHID) {
              count++;
            }
          }
          threadPrefixSums[tid] = count;
        }
      );

      // get prefix sums
      for (unsigned int i = 1; i < threadPrefixSums.size(); i++) {
        threadPrefixSums[i] += threadPrefixSums[i - 1];
      }

      assert(prefixSumOfEdges.size() == numNodes);
      assert(localToGlobalVector.size() == numNodes);

      uint32_t newMasterNodes = threadPrefixSums[activeThreads - 1];
      galois::gDebug("[", base_DistGraph::id, "] This many masters from host ",
                     h, ": ", newMasterNodes);
      uint32_t startingNodeIndex = numNodes;
      // increase size of prefix sum + mapping vector
      prefixSumOfEdges.resize(numNodes + newMasterNodes);
      localToGlobalVector.resize(numNodes + newMasterNodes);

      if (newMasterNodes > 0) {
        // do actual work, second on_each
        galois::on_each(
          [&] (unsigned tid, unsigned nthreads) {
            size_t beginNode;
            size_t endNode;
            std::tie(beginNode, endNode) = galois::block_range((size_t)0,
                                             hostSize, tid, nthreads);

            // start location to start adding things into prefix sums/vectors
            uint32_t threadStartLocation = 0;
            if (tid != 0) {
              threadStartLocation = threadPrefixSums[tid - 1];
            }

            uint32_t handledNodes = 0;
            for (size_t i = beginNode; i < endNode; i++) {
              uint32_t globalID = startNode + i;
              // if this node is master, get outgoing edges + save mapping
              if (graphPartitioner->getMaster(globalID) == myHID) {
                // check size
                if (numOutgoingEdges[h].size() > 0) {
                  uint64_t myEdges = numOutgoingEdges[h][i];
                  numOutgoingEdges[h][i] = 0; // set to 0; does not need to be
                                              // handled later
                  prefixSumOfEdges[startingNodeIndex + threadStartLocation +
                                   handledNodes] = myEdges;
                  if (myEdges > 0 && h != myHID) {
                    toReceive += 1;
                  }
                } else {
                  prefixSumOfEdges[startingNodeIndex + threadStartLocation +
                                   handledNodes] = 0;
                }

                localToGlobalVector[startingNodeIndex + threadStartLocation +
                                    handledNodes] = globalID;
                handledNodes++;
              }
            }
          }
        );
        numNodes += newMasterNodes;
      }
    }

    nodesToReceive += toReceive.reduce();
    // masters have been handled
    base_DistGraph::numOwned = numNodes;
  }

  /**
   * Outgoing inspection: loop over all nodes, determnine if outgoing exists;
   * if does, create mapping, get edges
   */
  void inspectOutgoingNodes(
    std::vector<std::vector<uint64_t>>& numOutgoingEdges,
    galois::gstl::Vector<uint64_t>& prefixSumOfEdges
  ) {
    uint32_t myHID = base_DistGraph::id;

    galois::GAccumulator<uint32_t> toReceive;
    toReceive.reset();

    for (unsigned h = 0; h < base_DistGraph::numHosts; ++h) {
      size_t hostSize = numOutgoingEdges[h].size();
      // if i got no outgoing info from this host, safely continue to next one
      if (hostSize == 0) {
        continue;
      }

      uint32_t activeThreads = galois::getActiveThreads();
      std::vector<uint64_t> threadPrefixSums(activeThreads);

      // for each thread, figure out how many items it will work with (only
      // owned nodes)
      galois::on_each(
        [&](unsigned tid, unsigned nthreads) {
          size_t beginNode;
          size_t endNode;
          std::tie(beginNode, endNode) = galois::block_range((size_t)0,
                                             hostSize, tid, nthreads);
          uint64_t count = 0;
          for (size_t i = beginNode; i < endNode; i++) {
            if (numOutgoingEdges[h][i] > 0) {
              count++;
            }
          }
          threadPrefixSums[tid] = count;
        }
      );

      // get prefix sums
      for (unsigned int i = 1; i < threadPrefixSums.size(); i++) {
        threadPrefixSums[i] += threadPrefixSums[i - 1];
      }

      assert(prefixSumOfEdges.size() == numNodes);
      assert(localToGlobalVector.size() == numNodes);

      uint32_t newOutgoingNodes = threadPrefixSums[activeThreads - 1];
      // increase size of prefix sum + mapping vector
      prefixSumOfEdges.resize(numNodes + newOutgoingNodes);
      localToGlobalVector.resize(numNodes + newOutgoingNodes);

      uint64_t startNode = base_DistGraph::gid2host[h].first;
      uint32_t startingNodeIndex = numNodes;


      if (newOutgoingNodes > 0) {
        // do actual work, second on_each
        galois::on_each(
          [&] (unsigned tid, unsigned nthreads) {
            size_t beginNode;
            size_t endNode;
            std::tie(beginNode, endNode) = galois::block_range((size_t)0,
                                             hostSize, tid, nthreads);

            // start location to start adding things into prefix sums/vectors
            uint32_t threadStartLocation = 0;
            if (tid != 0) {
              threadStartLocation = threadPrefixSums[tid - 1];
            }

            uint32_t handledNodes = 0;

            for (size_t i = beginNode; i < endNode; i++) {
              uint64_t myEdges = numOutgoingEdges[h][i];
              if (myEdges > 0) {
                prefixSumOfEdges[startingNodeIndex + threadStartLocation +
                                 handledNodes] = myEdges;
                localToGlobalVector[startingNodeIndex + threadStartLocation +
                                    handledNodes] = startNode + i;
                handledNodes++;

                if (myEdges > 0 && h != myHID) {
                  toReceive += 1;
                }
              }
            }
          }
        );
        numNodes += newOutgoingNodes;
      }
      // don't need anymore after this point; get memory back
      numOutgoingEdges[h].clear();
    }

    nodesToReceive += toReceive.reduce();
    base_DistGraph::numNodesWithEdges = numNodes;
  }

  /**
   * Create a part of the global to local map (it's missing the incoming
   * mirrors with no edges) + part of prefix sum
   *
   * @param[in, out] prefixSumOfEdges edge prefix sum to build
   * @param[in] incomingEstimate estimate of number of incoming nodes to build
   */
  void createIntermediateMetadata(
    galois::gstl::Vector<uint64_t>& prefixSumOfEdges,
    const uint64_t incomingEstimate
  ) {
    if (numNodes == 0) {
      return;
    }
    globalToLocalMap.reserve(base_DistGraph::numNodesWithEdges + incomingEstimate);
    globalToLocalMap[localToGlobalVector[0]] = 0;
    // global to local map construction using num nodes with edges
    for (unsigned i = 1; i < base_DistGraph::numNodesWithEdges; i++) {
      prefixSumOfEdges[i] += prefixSumOfEdges[i - 1];
      globalToLocalMap[localToGlobalVector[i]] = i;
    }
  }

  /**
   * incoming node creation if is doesn't already exist + if actually amrked
   * as having incoming node
   */
  void inspectIncomingNodes(galois::DynamicBitSet& hasIncomingEdge,
                            galois::gstl::Vector<uint64_t>& prefixSumOfEdges) {
    uint32_t totalNumNodes = base_DistGraph::numGlobalNodes;

    uint32_t activeThreads = galois::getActiveThreads();
    std::vector<uint64_t> threadPrefixSums(activeThreads);

    galois::on_each(
      [&](unsigned tid, unsigned nthreads) {
        size_t beginNode;
        size_t endNode;
        std::tie(beginNode, endNode) = galois::block_range(0u,
                                         totalNumNodes, tid, nthreads);
        uint64_t count = 0;
        for (size_t i = beginNode; i < endNode; i++) {
          // only count if doesn't exist in global/local map + is incoming
          // edge
          if (hasIncomingEdge.test(i) && !globalToLocalMap.count(i)) ++count;
        }
        threadPrefixSums[tid] = count;
      }
    );
    // get prefix sums
    for (unsigned int i = 1; i < threadPrefixSums.size(); i++) {
      threadPrefixSums[i] += threadPrefixSums[i - 1];
    }

    assert(prefixSumOfEdges.size() == numNodes);
    assert(localToGlobalVector.size() == numNodes);

    uint32_t newIncomingNodes = threadPrefixSums[activeThreads - 1];
    // increase size of prefix sum + mapping vector
    prefixSumOfEdges.resize(numNodes + newIncomingNodes);
    localToGlobalVector.resize(numNodes + newIncomingNodes);

    uint32_t startingNodeIndex = numNodes;

    if (newIncomingNodes > 0) {
      // do actual work, second on_each
      galois::on_each(
        [&] (unsigned tid, unsigned nthreads) {
          size_t beginNode;
          size_t endNode;
          std::tie(beginNode, endNode) = galois::block_range(0u,
                                           totalNumNodes, tid, nthreads);

          // start location to start adding things into prefix sums/vectors
          uint32_t threadStartLocation = 0;
          if (tid != 0) {
            threadStartLocation = threadPrefixSums[tid - 1];
          }

          uint32_t handledNodes = 0;

          for (size_t i = beginNode; i < endNode; i++) {
            if (hasIncomingEdge.test(i) && !globalToLocalMap.count(i)) {
              prefixSumOfEdges[startingNodeIndex + threadStartLocation +
                               handledNodes] = 0;
              localToGlobalVector[startingNodeIndex + threadStartLocation +
                                  handledNodes] = i;
              handledNodes++;
            }
          }
        }
      );
      numNodes += newIncomingNodes;
    }
  }

  /**
   * finalize metadata maps
   */
  void finalizeInspection(galois::gstl::Vector<uint64_t>& prefixSumOfEdges) {
    // reserve rest of memory needed
    globalToLocalMap.reserve(numNodes);
    for (unsigned i = base_DistGraph::numNodesWithEdges; i < numNodes; i++) {
      // finalize prefix sum
      prefixSumOfEdges[i] += prefixSumOfEdges[i - 1];
      // global to local map construction
      globalToLocalMap[localToGlobalVector[i]] = i;
    }
    if (prefixSumOfEdges.size() != 0) {
      numEdges = prefixSumOfEdges.back();
    } else {
      numEdges = 0;
    }
  }

////////////////////////////////////////////////////////////////////////////////

  /**
   * Fill up mirror arrays.
   * TODO make parallel?
   */
  void fillMirrors() {
    base_DistGraph::mirrorNodes.reserve(numNodes - base_DistGraph::numOwned);
    for (uint32_t i = base_DistGraph::numOwned; i < numNodes; i++) {
      uint32_t globalID = localToGlobalVector[i];
      base_DistGraph::mirrorNodes[graphPartitioner->getMaster(globalID)].
              push_back(globalID);
    }
  }

////////////////////////////////////////////////////////////////////////////////

  template <typename GraphTy>
  void loadEdges(GraphTy& graph,
                 galois::graphs::BufferedGraph<EdgeTy>& bufGraph) {
    if (base_DistGraph::id == 0) {
      if (std::is_void<typename GraphTy::edge_data_type>::value) {
        fprintf(stderr, "Loading void edge-data while creating edges.\n");
      } else {
        fprintf(stderr, "Loading edge-data while creating edges.\n");
      }
    }

    bufGraph.resetReadCounters();

    std::atomic<uint32_t> receivedNodes;
    receivedNodes.store(0);

    galois::StatTimer loadEdgeTimer("EdgeLoading", GRNAME);
    loadEdgeTimer.start();

    // sends data
    sendEdges(graph, bufGraph, receivedNodes);
    uint64_t bufBytesRead = bufGraph.getBytesRead();
    // get data from graph back (don't need it after sending things out)
    bufGraph.resetAndFree();

    // receives data
    galois::on_each([&](unsigned tid, unsigned nthreads) {
      receiveEdges(graph, receivedNodes);
    });
    base_DistGraph::increment_evilPhase();

    loadEdgeTimer.stop();

    galois::gPrint("[", base_DistGraph::id, "] Edge loading time: ",
                   loadEdgeTimer.get_usec() / 1000000.0f,
                   " seconds to read ", bufBytesRead, " bytes (",
                   bufBytesRead / (float)loadEdgeTimer.get_usec(),
                   " MBPS)\n");
  }

  // Edge type is not void. (i.e. edge data exists)
  template <typename GraphTy,
            typename std::enable_if<!std::is_void<
                typename GraphTy::edge_data_type>::value>::type* = nullptr>
  void sendEdges(GraphTy& graph,
                 galois::graphs::BufferedGraph<EdgeTy>& bufGraph,
                 std::atomic<uint32_t>& receivedNodes) {
    using DstVecType = std::vector<std::vector<uint64_t>>;
    using DataVecType =
        std::vector<std::vector<typename GraphTy::edge_data_type>>;
    using SendBufferVecTy = std::vector<galois::runtime::SendBuffer>;

    galois::substrate::PerThreadStorage<DstVecType> gdst_vecs(
        base_DistGraph::numHosts);
    galois::substrate::PerThreadStorage<DataVecType> gdata_vecs(
        base_DistGraph::numHosts);
    galois::substrate::PerThreadStorage<SendBufferVecTy> sendBuffers(
        base_DistGraph::numHosts);

    auto& net             = galois::runtime::getSystemNetworkInterface();
    const unsigned& id       = this->base_DistGraph::id;
    const unsigned& numHosts = this->base_DistGraph::numHosts;

    galois::GAccumulator<uint64_t> messagesSent;
    galois::GAccumulator<uint64_t> bytesSent;
    galois::GReduceMax<uint64_t> maxBytesSent;
    messagesSent.reset();
    bytesSent.reset();
    maxBytesSent.reset();

    // Go over assigned nodes and distribute edges.
    galois::do_all(
      galois::iterate(base_DistGraph::gid2host[base_DistGraph::id].first,
                      base_DistGraph::gid2host[base_DistGraph::id].second),
      [&](auto src) {
        uint32_t lsrc       = 0;
        uint64_t curEdge    = 0;
        if (this->isLocal(src)) {
          lsrc = this->G2L(src);
          curEdge = *graph.edge_begin(lsrc, galois::MethodFlag::UNPROTECTED);
        }

        auto ee     = bufGraph.edgeBegin(src);
        auto ee_end = bufGraph.edgeEnd(src);
        uint64_t numEdgesL = std::distance(ee, ee_end);
        auto& gdst_vec  = *gdst_vecs.getLocal();
        auto& gdata_vec = *gdata_vecs.getLocal();

        for (unsigned i = 0; i < numHosts; ++i) {
          gdst_vec[i].clear();
          gdata_vec[i].clear();
          gdst_vec[i].reserve(numEdgesL);
          //gdata_vec[i].reserve(numEdgesL);
        }

        for (; ee != ee_end; ++ee) {
          uint32_t gdst = bufGraph.edgeDestination(*ee);
          auto gdata    = bufGraph.edgeData(*ee);

          uint32_t hostBelongs =
            graphPartitioner->getEdgeOwner(src, gdst, numEdgesL);
          if (hostBelongs == id) {
            // edge belongs here, construct on self
            assert(this->isLocal(src));
            uint32_t ldst = this->G2L(gdst);
            graph.constructEdge(curEdge++, ldst, gdata);
            // TODO
            // if ldst is an outgoing mirror, this is vertex cut
          } else {
            // add to host vector to send out later
            gdst_vec[hostBelongs].push_back(gdst);
            gdata_vec[hostBelongs].push_back(gdata);
          }
        }

        // make sure all edges accounted for if local
        if (this->isLocal(src)) {
          assert(curEdge == (*graph.edge_end(lsrc)));
        }

        // send
        for (uint32_t h = 0; h < numHosts; ++h) {
          if (h == id) continue;

          if (gdst_vec[h].size() > 0) {
            auto& b = (*sendBuffers.getLocal())[h];
            galois::runtime::gSerialize(b, src);
            galois::runtime::gSerialize(b, gdst_vec[h]);
            galois::runtime::gSerialize(b, gdata_vec[h]);

            // send if over limit
            if (b.size() > edgePartitionSendBufSize) {
              messagesSent += 1;
              bytesSent.update(b.size());
              maxBytesSent.update(b.size());

              net.sendTagged(h, galois::runtime::evilPhase, b);
              b.getVec().clear();
              b.getVec().reserve(edgePartitionSendBufSize * 1.25);
            }
          }
        }

        // overlap receives
        auto buffer = net.recieveTagged(galois::runtime::evilPhase, nullptr);
        this->processReceivedEdgeBuffer(buffer, graph, receivedNodes);
      },
      #if MORE_DIST_STATS
      galois::loopname("EdgeLoadingLoop"),
      #endif
      galois::steal(),
      galois::no_stats()
    );

    // flush buffers
    for (unsigned threadNum = 0; threadNum < sendBuffers.size(); ++threadNum) {
      auto& sbr = *sendBuffers.getRemote(threadNum);
      for (unsigned h = 0; h < this->base_DistGraph::numHosts; ++h) {
        if (h == this->base_DistGraph::id) continue;
        auto& sendBuffer = sbr[h];
        if (sendBuffer.size() > 0) {
          messagesSent += 1;
          bytesSent.update(sendBuffer.size());
          maxBytesSent.update(sendBuffer.size());

          net.sendTagged(h, galois::runtime::evilPhase, sendBuffer);
          sendBuffer.getVec().clear();
        }
      }
    }

    net.flush();

    galois::runtime::reportStat_Tsum(
      GRNAME, std::string("EdgeLoadingMessagesSent"), messagesSent.reduce()
    );
    galois::runtime::reportStat_Tsum(
      GRNAME, std::string("EdgeLoadingBytesSent"), bytesSent.reduce()
    );
    galois::runtime::reportStat_Tmax(
      GRNAME, std::string("EdgeLoadingMaxBytesSent"), maxBytesSent.reduce()
    );
  }

  // no edge data version
  template <typename GraphTy,
            typename std::enable_if<std::is_void<
                typename GraphTy::edge_data_type>::value>::type* = nullptr>
  void sendEdges(GraphTy& graph,
                 galois::graphs::BufferedGraph<EdgeTy>& bufGraph,
                 std::atomic<uint32_t>& receivedNodes) {
    using DstVecType = std::vector<std::vector<uint64_t>>;
    using SendBufferVecTy = std::vector<galois::runtime::SendBuffer>;

    galois::substrate::PerThreadStorage<DstVecType> gdst_vecs(
        base_DistGraph::numHosts);
    galois::substrate::PerThreadStorage<SendBufferVecTy> sendBuffers(
        base_DistGraph::numHosts);

    auto& net                = galois::runtime::getSystemNetworkInterface();
    const unsigned& id       = this->base_DistGraph::id;
    const unsigned& numHosts = this->base_DistGraph::numHosts;

    galois::GAccumulator<uint64_t> messagesSent;
    galois::GAccumulator<uint64_t> bytesSent;
    galois::GReduceMax<uint64_t> maxBytesSent;
    messagesSent.reset();
    bytesSent.reset();
    maxBytesSent.reset();

    // Go over assigned nodes and distribute edges.
    galois::do_all(
      galois::iterate(base_DistGraph::gid2host[base_DistGraph::id].first,
                      base_DistGraph::gid2host[base_DistGraph::id].second),
      [&](auto src) {
        uint32_t lsrc       = 0;
        uint64_t curEdge    = 0;
        if (this->isLocal(src)) {
          lsrc = this->G2L(src);
          curEdge = *graph.edge_begin(lsrc, galois::MethodFlag::UNPROTECTED);
        }

        auto ee     = bufGraph.edgeBegin(src);
        auto ee_end = bufGraph.edgeEnd(src);
        uint64_t numEdges = std::distance(ee, ee_end);
        auto& gdst_vec  = *gdst_vecs.getLocal();

        for (unsigned i = 0; i < numHosts; ++i) {
          gdst_vec[i].clear();
          //gdst_vec[i].reserve(numEdges);
        }

        for (; ee != ee_end; ++ee) {
          uint32_t gdst = bufGraph.edgeDestination(*ee);
          uint32_t hostBelongs =
            graphPartitioner->getEdgeOwner(src, gdst, numEdges);

          if (hostBelongs == id) {
            // edge belongs here, construct on self
            assert(this->isLocal(src));
            uint32_t ldst = this->G2L(gdst);
            graph.constructEdge(curEdge++, ldst);
            // TODO
            // if ldst is an outgoing mirror, this is vertex cut
          } else {
            // add to host vector to send out later
            gdst_vec[hostBelongs].push_back(gdst);
          }
        }

        // make sure all edges accounted for if local
        if (this->isLocal(src)) {
          assert(curEdge == (*graph.edge_end(lsrc)));
        }

        // send
        for (uint32_t h = 0; h < numHosts; ++h) {
          if (h == id) continue;

          if (gdst_vec[h].size() > 0) {
            auto& b = (*sendBuffers.getLocal())[h];
            galois::runtime::gSerialize(b, src);
            galois::runtime::gSerialize(b, gdst_vec[h]);

            // send if over limit
            if (b.size() > edgePartitionSendBufSize) {
              messagesSent += 1;
              bytesSent.update(b.size());
              maxBytesSent.update(b.size());

              net.sendTagged(h, galois::runtime::evilPhase, b);
              b.getVec().clear();
              b.getVec().reserve(edgePartitionSendBufSize * 1.25);
            }
          }
        }

        // overlap receives
        auto buffer = net.recieveTagged(galois::runtime::evilPhase, nullptr);
        this->processReceivedEdgeBuffer(buffer, graph, receivedNodes);
      },
      #if MORE_DIST_STATS
      galois::loopname("EdgeLoading"),
      #endif
      galois::steal(),
      galois::no_stats()
    );

    // flush buffers
    for (unsigned threadNum = 0; threadNum < sendBuffers.size(); ++threadNum) {
      auto& sbr = *sendBuffers.getRemote(threadNum);
      for (unsigned h = 0; h < this->base_DistGraph::numHosts; ++h) {
        if (h == this->base_DistGraph::id) continue;
        auto& sendBuffer = sbr[h];
        if (sendBuffer.size() > 0) {
          messagesSent += 1;
          bytesSent.update(sendBuffer.size());
          maxBytesSent.update(sendBuffer.size());

          net.sendTagged(h, galois::runtime::evilPhase, sendBuffer);
          sendBuffer.getVec().clear();
        }
      }
    }

    net.flush();

    galois::runtime::reportStat_Tsum(
      GRNAME, std::string("EdgeLoadingMessagesSent"), messagesSent.reduce()
    );
    galois::runtime::reportStat_Tsum(
      GRNAME, std::string("EdgeLoadingBytesSent"), bytesSent.reduce()
    );
    galois::runtime::reportStat_Tmax(
      GRNAME, std::string("EdgeLoadingMaxBytesSent"), maxBytesSent.reduce()
    );
  }

  //! Optional type
  //! @tparam T type that the variable may possibly take
  template <typename T>
#if __GNUC__ > 5 || (__GNUC__ == 5 && __GNUC_MINOR__ > 1)
  using optional_t = std::experimental::optional<T>;
#else
  using optional_t = boost::optional<T>;
#endif
  //! @copydoc DistGraphHybridCut::processReceivedEdgeBuffer
  template <typename GraphTy>
  void processReceivedEdgeBuffer(
      optional_t<std::pair<uint32_t, galois::runtime::RecvBuffer>>& buffer,
      GraphTy& graph, std::atomic<uint32_t>& receivedNodes) {
    if (buffer) {
      auto& rb = buffer->second;
      while (rb.r_size() > 0) {
        uint64_t n;
        std::vector<uint64_t> gdst_vec;
        galois::runtime::gDeserialize(rb, n);
        galois::runtime::gDeserialize(rb, gdst_vec);
        assert(isLocal(n));
        uint32_t lsrc = G2L(n);
        uint64_t cur = *graph.edge_begin(lsrc, galois::MethodFlag::UNPROTECTED);
        uint64_t cur_end = *graph.edge_end(lsrc);
        assert((cur_end - cur) == gdst_vec.size());
        deserializeEdges(graph, rb, gdst_vec, cur, cur_end);
        ++receivedNodes;
      }
    }
  }

  /**
   * Receive the edge dest/data assigned to this host from other hosts
   * that were responsible for reading them.
   */
  template <typename GraphTy>
  void receiveEdges(GraphTy& graph, std::atomic<uint32_t>& receivedNodes) {
    auto& net = galois::runtime::getSystemNetworkInterface();

    // receive edges for all mirror nodes
    while (receivedNodes < nodesToReceive) {
      decltype(net.recieveTagged(galois::runtime::evilPhase, nullptr)) p;
      p = net.recieveTagged(galois::runtime::evilPhase, nullptr);
      processReceivedEdgeBuffer(p, graph, receivedNodes);
    }
  }

  template <typename GraphTy,
            typename std::enable_if<!std::is_void<
                typename GraphTy::edge_data_type>::value>::type* = nullptr>
  void deserializeEdges(GraphTy& graph, galois::runtime::RecvBuffer& b,
                        std::vector<uint64_t>& gdst_vec, uint64_t& cur,
                        uint64_t& cur_end) {
    std::vector<typename GraphTy::edge_data_type> gdata_vec;
    galois::runtime::gDeserialize(b, gdata_vec);
    uint64_t i = 0;
    while (cur < cur_end) {
      auto gdata    = gdata_vec[i];
      uint64_t gdst = gdst_vec[i++];
      uint32_t ldst = G2L(gdst);
      graph.constructEdge(cur++, ldst, gdata);
      // TODO
      // if ldst is an outgoing mirror, this is vertex cut
    }
  }

  template <typename GraphTy,
            typename std::enable_if<std::is_void<
                typename GraphTy::edge_data_type>::value>::type* = nullptr>
  void deserializeEdges(GraphTy& graph, galois::runtime::RecvBuffer& b,
                        std::vector<uint64_t>& gdst_vec, uint64_t& cur,
                        uint64_t& cur_end) {
    uint64_t i = 0;
    while (cur < cur_end) {
      uint64_t gdst = gdst_vec[i++];
      uint32_t ldst = G2L(gdst);
      graph.constructEdge(cur++, ldst);
      // TODO
      // if ldst is an outgoing mirror, this is vertex cut
    }
  }

 public:
  /**
   * Reset bitset
   */
  void reset_bitset(typename base_DistGraph::SyncType syncType,
                    void (*bitset_reset_range)(size_t, size_t)) const {
    // layout: masters.... outgoing mirrors.... incoming mirrors
    // note the range for bitset reset range is inclusive
    if (base_DistGraph::numOwned > 0) {
      if (syncType == base_DistGraph::syncBroadcast) { // reset masters
        bitset_reset_range(0, base_DistGraph::numOwned - 1);
      } else {
        assert(syncType == base_DistGraph::syncReduce);
        // mirrors occur after masters
        if (base_DistGraph::numOwned < numNodes) {
          bitset_reset_range(base_DistGraph::numOwned, numNodes - 1);
        }
      }
    } else { // all things are mirrors
      // only need to reset if reduce
      if (syncType == base_DistGraph::syncReduce) {
        if (numNodes > 0) {
          bitset_reset_range(0, numNodes - 1);
        }
      }
    }
  }

  std::vector<std::pair<uint32_t, uint32_t>> getMirrorRanges() const {
    std::vector<std::pair<uint32_t, uint32_t>> mirrorRangesVector;
    // order of nodes locally is masters, outgoing mirrors, incoming mirrors,
    // so just get from numOwned to end
    mirrorRangesVector.push_back(std::make_pair(base_DistGraph::numOwned,
                                                numNodes));
    return mirrorRangesVector;
  }

  // TODO current uses graph partitioner
  // TODO make it so user doens't have to specify; can be done by tracking
  // if an outgoing mirror is marked as having an incoming edge on any
  // host
  bool is_vertex_cut() const {
    return graphPartitioner->isVertexCut();
  }

  virtual void boostSerializeLocalGraph(boost::archive::binary_oarchive& ar,
                                        const unsigned int version = 0) const {
    // unsigned ints
    ar << numNodes;

    // partition specific
    graphPartitioner->serializePartition(ar);

    // maps and vectors
    ar << localToGlobalVector;
    ar << globalToLocalMap;
  }

  virtual void boostDeSerializeLocalGraph(boost::archive::binary_iarchive& ar,
                                          const unsigned int version = 0) {
    graphPartitioner = new Partitioner(base_DistGraph::id, base_DistGraph::numHosts,
                                       base_DistGraph::numGlobalNodes,
                                       base_DistGraph::numGlobalEdges);

    graphPartitioner->saveGIDToHost(base_DistGraph::gid2host);

    // unsigned ints
    ar >> numNodes;

    // partition specific
    graphPartitioner->deserializePartition(ar);

    // maps and vectors
    ar >> localToGlobalVector;
    ar >> globalToLocalMap;
  }
};

// make GRNAME visible to public
template <typename NodeTy, typename EdgeTy, typename Partitioner>
constexpr const char* const
    galois::graphs::NewDistGraphGeneric<NodeTy, EdgeTy, Partitioner>::GRNAME;

} // end namespace graphs
} // end namespace galois
#endif
