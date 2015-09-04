/*
 * LC_LinearArray_Graph.h
 *
 *  Created on: Feb 27, 2013
 *  Single array representation, has incoming and outgoing edges.
 *      Author: rashid
 */

#include <iostream>
#include <fstream>
#include <cstring>
#include <algorithm>
#include <string>
#include <vector>
#include <map>
#include <limits>

#ifndef LC_LinearArray_Graph_H_
#define LC_LinearArray_Graph_H_

namespace Galois {
namespace OpenCL {
static const char * _str_LC_LinearArray_Graph = "typedef struct _GraphType { \n"
      "uint _num_nodes;\n"
      "uint _num_edges;\n "
      "uint node_data_size;\n "
      "uint edge_data_size;\n "
      "__global uint *in_index;\n "
      "__global uint *in_neighbors;\n "
      "__global EdgeData *in_edge_data;\n "
      "__global NodeData*node_data;\n "
      "__global uint *out_index;\n "
      "__global uint *out_neighbors;\n "
      "__global EdgeData *out_edge_data;\n }GraphType;\n"
      "uint in_neighbors_begin(__local GraphType * graph, uint node){ \n return 0;\n}\n"
      "uint in_neighbors_end(__local GraphType * graph, uint node){ \n return graph->in_index[node+1]-graph->in_index[node];\n}\n"
      "uint in_neighbors_next(__local GraphType * graph, uint node){ \n return 1;\n}\n"
      "uint out_neighbors_begin(__local GraphType * graph, uint node){ \n return 0;\n}\n"
      "uint out_neighbors_end(__local GraphType * graph, uint node){ \n return graph->out_index[node+1]-graph->out_index[node];\n}\n"
      "uint out_neighbors_next(__local GraphType * graph, uint node){ \n return 1;\n}\n"
      "uint in_neighbors(__local GraphType * graph, uint node, uint nbr){ \n return graph->in_neighbors[graph->in_index[node]+nbr];\n}\n"
      "uint out_neighbors(__local GraphType * graph,uint node,  uint nbr){ \n return graph->out_neighbors[graph->out_index[node]+nbr];\n}\n"
      "__global NodeData * node_data(__local GraphType * graph, uint node){ \n return &graph->node_data[node];\n}\n"
      "__global EdgeData * in_edge_data(__local GraphType * graph, uint node, uint nbr){ \n return &graph->in_edge_data[graph->in_index[node]+nbr];\n}\n"
      "__global EdgeData * out_edge_data(__local GraphType * graph,uint node,  uint nbr){ \n return &graph->out_edge_data[graph->out_index[node]+nbr];\n}\n"
      "void initialize(__local GraphType * graph, __global uint *mem_pool){\nuint offset =4;\n graph->_num_nodes=mem_pool[0];\n"
      "graph->_num_edges=mem_pool[1];\n graph->node_data_size =mem_pool[2];\n graph->edge_data_size=mem_pool[3];\n"
      "graph->node_data= (__global NodeData *)&mem_pool[offset];\n offset +=graph->_num_nodes* graph->node_data_size;\n"
      "graph->out_index=&mem_pool[offset];\n offset +=graph->_num_nodes + 1;\n graph->out_neighbors=&mem_pool[offset];\n"
      "offset +=graph->_num_edges;\n"
      "graph->out_edge_data=(__global EdgeData*)&mem_pool[offset];\n"
      "offset +=graph->_num_edges*graph->edge_data_size;\n"
      "graph->in_index=& mem_pool[offset];\n offset +=graph->_num_nodes+1;\n graph->in_neighbors=& mem_pool[offset];\n offset +=graph->_num_edges;\n"
      "graph->in_edge_data=& mem_pool[offset];\n}\n";

template<template<typename > class GPUWrapper, typename NodeDataTy, typename EdgeDataTy>

struct LC_LinearArray_Graph {
   //Are you using gcc/4.7+ Error on line below for earlier versions.
   #ifdef _WIN32
		typedef GPUWrapper<unsigned int> GPUType;
        typedef typename GPUWrapper<unsigned int>::HostPtrType HostPtrType;
        typedef typename GPUWrapper<unsigned int>::DevicePtrType DevicePtrType;
#else
		template<typename T> using ArrayType = GPUWrapper<T>;
		        //   typedef GPUWrapper<unsigned int> ArrayType;
        typedef GPUWrapper<unsigned int> GPUType;
        typedef typename GPUWrapper<unsigned int>::HostPtrType HostPtrType;
        typedef typename GPUWrapper<unsigned int>::DevicePtrType DevicePtrType;
#endif
	typedef NodeDataTy NodeDataType;
   typedef EdgeDataTy EdgeDataType;
   typedef unsigned int NodeIDType;
   typedef unsigned int EdgeIDType;
   size_t _num_nodes;
   size_t _num_edges;
   unsigned int _max_degree;
   const size_t SizeEdgeData;
   const size_t SizeNodeData;
   GPUType * gpu_graph;
/////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////
   LC_LinearArray_Graph() :
         SizeEdgeData(sizeof(EdgeDataType) / sizeof(unsigned int)), SizeNodeData(sizeof(NodeDataType) / sizeof(unsigned int)){
      fprintf(stderr, "Created LC_LinearArray_Graph with %d node %d edge data. ",(int)SizeNodeData,(int)SizeEdgeData);
//      std::cout << "Created LC_LinearArray_Graph with " << SizeNodeData << " node " << SizeEdgeData << " edge data. ";
      _max_degree = _num_nodes = _num_edges = 0;
      gpu_graph = 0;
   }
   ~LC_LinearArray_Graph(){
      deallocate();
   }
   void read(const char * filename) {
      readFromGR(*this, filename);
   }
   size_t size(){
      return gpu_graph->size();
   }
   NodeDataType * getData() {
      return (NodeDataType*) (gpu_graph->host_data + 4);
   }
   NodeDataType & getData(NodeIDType nid) {
         return ((NodeDataType*) (gpu_graph->host_data + 4))[nid];
      }
   unsigned int * outgoing_index() {
      return (unsigned int*) (getData()) + _num_nodes * SizeNodeData;
   }
   unsigned int num_neighbors(NodeIDType node) {
      return outgoing_index()[node + 1] - outgoing_index()[node];
   }
   unsigned int * out_neighbors() {
      return (unsigned int *) outgoing_index() + _num_nodes + 1;
   }
   unsigned int & out_neighbors(NodeIDType node, unsigned int idx) {
      return out_neighbors()[node + idx];
   }

   EdgeDataType * out_edge_data() {
      return (EdgeDataType *) (unsigned int *) (out_neighbors()) + _num_edges;
   }
   EdgeDataType &out_edge_data(NodeIDType node_id, int nbr_id) {
      return out_edge_data()[outgoing_index()[node_id] + nbr_id];
   }
   unsigned int * incoming_index() {
      return (unsigned int *) out_edge_data() + _num_edges * SizeEdgeData;
   }
   unsigned int * in_neighbors() {
      return (unsigned int *) incoming_index() + _num_nodes + 1;
   }
   EdgeDataType * in_edge_data() {
      return (EdgeDataType *) in_neighbors() + _num_edges;
   }
   unsigned int * last() {
      return (unsigned int *) in_edge_data() + _num_edges * SizeEdgeData;
   }
   GPUType * get_array_ptr(void) {
      return gpu_graph;
   }
   size_t num_nodes() {
      return _num_nodes;
   }
   size_t num_edges() {
      return _num_edges;
   }
   size_t max_degree() {
      return _max_degree;
   }
   void init(size_t n_n, size_t n_e) {
      _num_nodes = n_n;
      _num_edges = n_e;
      fprintf(stderr, "Allocating NN: :%d,  , NE %d :\n", (int)_num_nodes ,(int)_num_edges );
//      std::cout << "Allocating NN: " << _num_nodes << " , NE :" << _num_edges << ". ";
      //Num_nodes, num_edges, [node_data] , [outgoing_index], [out_neighbors], [edge_data], [incoming_index] , [incoming_neighbors]
      gpu_graph = new GPUType(4 + _num_nodes * SizeNodeData + _num_nodes + 1 + _num_edges + _num_edges * SizeEdgeData + _num_edges + 1 + _num_edges + _num_edges * SizeEdgeData);
      (*gpu_graph)[0] = (int) _num_nodes;
      (*gpu_graph)[1] = (int) _num_edges;
      (*gpu_graph)[2] = (int) SizeNodeData;
      (*gpu_graph)[3] = (int) SizeEdgeData;
      //allocate_on_gpu();
   }
   template<typename GaloisGraph>
      void load_from_galois(GaloisGraph & ggraph) {
         typedef typename GaloisGraph::GraphNode GNode;
         const size_t gg_num_nodes = ggraph.size();
         const size_t gg_num_edges = ggraph.sizeEdges();
         init(gg_num_nodes, gg_num_edges);
         const int * ptr = (int *) this->gpu_graph->host_ptr();
         int edge_counter = 0;
         int node_counter = 0;
         for (auto n = ggraph.begin(); n != ggraph.end(); n++, node_counter++) {
            int src_node = *n;
            getData()[src_node] = ggraph.getData(*n);
            outgoing_index()[src_node] = edge_counter;
            for (auto nbr = ggraph.edge_begin(*n); nbr != ggraph.edge_end(*n); ++nbr) {
               GNode dst = ggraph.getEdgeDst(*nbr);
               EdgeDataType ewt = ggraph.getEdgeData(*nbr);
               out_neighbors()[edge_counter] = dst;
               out_edge_data()[edge_counter] = ewt;
               edge_counter++;
               fprintf(stderr, "Edge::[%d,%d,%d]", src_node, dst, ewt);
            }
         }
//         if(node_counter!=gg_num_nodes)fprintf(stderr, "FAILED EDGE-COMPACTION :: %d, %d\n", node_counter, gg_num_nodes);
         outgoing_index()[gg_num_nodes] = edge_counter;
         assert(edge_counter == gg_num_edges && "Failed to add all edges.");
      }

      template<typename GaloisGraph>
         void load_from_galois(GaloisGraph & ggraph, int gg_num_nodes, int gg_num_edges, int num_ghosts) {
            typedef typename GaloisGraph::GraphNode GNode;
      //      const size_t gg_num_nodes = ggraph.size();
      //      const size_t gg_num_edges = ggraph.sizeEdges();
            init(gg_num_nodes+num_ghosts, gg_num_edges);
            const int * ptr = (int *) this->gpu_graph->host_ptr();
            fprintf(stderr, "Loading from GaloisGraph [%d,%d,%d].\n", (int)gg_num_nodes, (int)gg_num_edges, num_ghosts);
            int edge_counter = 0;
            int node_counter = 0;
            for (auto n = ggraph.begin(); n != ggraph.begin()+gg_num_nodes; n++, node_counter++) {
               int src_node = *n;
               memcpy(&getData()[src_node], &ggraph.getData(*n), sizeof(NodeDataTy));
               outgoing_index()[src_node] = edge_counter;
               for (auto nbr = ggraph.edge_begin(*n); nbr != ggraph.edge_end(*n); ++nbr) {
   //               GNode dst = ggraph.getEdgeDst(*nbr);
   //               out_neighbors()[edge_counter] = dst;
                  GNode dst = ggraph.getEdgeDst(*nbr);
                  EdgeDataType ewt = ggraph.getEdgeData(*nbr);
                  out_neighbors()[edge_counter] = dst;
                  out_edge_data()[edge_counter] = ewt;
                  if(dst==0)
                  fprintf(stderr, "Edge::[%d,%d,%d]", src_node, dst, ewt);
                  edge_counter++;
               }
            }
            for(;node_counter<gg_num_nodes+num_ghosts;node_counter++){
               outgoing_index()[node_counter] = edge_counter;
            }
            outgoing_index()[gg_num_nodes] = edge_counter;
            if(node_counter!=gg_num_nodes)fprintf(stderr, "FAILED EDGE-COMPACTION :: %d, %d, %d\n", node_counter, gg_num_nodes, num_ghosts);
            assert(edge_counter == gg_num_edges && "Failed to add all edges.");
      //      fprintf(stderr, "Loaded from GaloisGraph [%d,%d,%d,%d].\n", ptr[0], ptr[1], ptr[2], ptr[3]);
         }
   /////////////////////////////////////////////////////////////////////////////////////////////
   /////////////////////////////////////////////////////////////////////////////////////////////
   void copy_to_device(void) {
      gpu_graph->copy_to_device();
   }
   /////////////////////////////////////////////////////////////////////////////////////////////
   /////////////////////////////////////////////////////////////////////////////////////////////
   void copy_to_host(void) {
      gpu_graph->copy_to_host();
   }
   DevicePtrType & device_ptr() {
      return gpu_graph->device_ptr();
   }
   HostPtrType & host_ptr(void) {
      return gpu_graph->host_ptr();
   }
   void print_header(void) {
      std::cout << "Header :: [";
      for (unsigned int i = 0; i < 6; ++i) {
         std::cout << gpu_graph->operator[](i) << ",";
      }
      std::cout << "\n";
      return;
   }
/////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////
   void print_node(unsigned int idx, const char * post = "") {
      if (idx < _num_nodes) {
         std::cout << "N-" << idx << "(" << (getData())[idx] << ")" << " :: [";
         for (size_t i = (outgoing_index())[idx]; i < (outgoing_index())[idx + 1]; ++i) {
            //std::cout << " " << (neighbors())[i] << "(" << (edge_data())[i] << "), ";
            std::cout << " " << (out_neighbors())[i] << "(" << (out_edge_data())[i] << "<" << getData()[out_neighbors()[i]] << ">" << "), ";
         }
         std::cout << "]##[";
         for (size_t i = (incoming_index())[idx]; i < (incoming_index())[idx + 1]; ++i) {
            //std::cout << " " << (in_neighbors())[i] << "(" << (in_edge_data())[i] << "), ";
            std::cout << " " << (in_neighbors())[i] << "(" << (in_edge_data())[i] << "<" << getData()[in_neighbors()[i]] << ">" << "), ";
         }
         std::cout << "]" << post;
      }
      return;
   }
   void print_node_nobuff(unsigned int idx, const char * post = "") {
         if (idx < _num_nodes) {
            fprintf(stderr, "N-%d(%d)::[",idx ,(getData())[idx] );
            for (size_t i = (outgoing_index())[idx]; i < (outgoing_index())[idx + 1]; ++i) {
               //std::cout << " " << (neighbors())[i] << "(" << (edge_data())[i] << "), ";
               fprintf(stderr, "%d ( < %d(%d) > ),  " ,(out_neighbors())[i] , (out_edge_data())[i] , getData()[out_neighbors()[i]] );
            }
            fprintf(stderr, "]##[");
            for (size_t i = (incoming_index())[idx]; i < (incoming_index())[idx + 1]; ++i) {
               //std::cout << " " << (in_neighbors())[i] << "(" << (in_edge_data())[i] << "), ";
               fprintf(stderr, "%d( %d <%d> ), ",(in_neighbors())[i] , (in_edge_data())[i] ,getData()[in_neighbors()[i]] );
            }
            fprintf(stderr, "]%s",post);
         }
         return;
      }

   static const char * get_graph_decl(std::string  &res) {
      res.append(_str_LC_LinearArray_Graph);
      return _str_LC_LinearArray_Graph;
   }
   /////////////////////////////////////////////////////////////////////////////////////////////
   /////////////////////////////////////////////////////////////////////////////////////////////
   void allocate_on_gpu() {
      return;
   }
/////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////
   void print_graph(void) {
      std::cout << "\n====Printing graph (" << _num_nodes << " , " << _num_edges << ")=====\n";
      for (size_t i = 0; i < _num_nodes; ++i) {
         print_node(i);
         std::cout << "\n";
      }
      return;
   }

/////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////
   void print_graph_nobuff(void) {
         fprintf(stderr, "\n====Printing graph (%d ,%d )=====\n", (int)_num_nodes,(int)_num_edges) ;
         for (size_t i = 0; i < _num_nodes; ++i) {
            print_node_nobuff(i, "\n");
         }
         return;
      }

   /////////////////////////////////////////////////////////////////////////////////////////////
   /////////////////////////////////////////////////////////////////////////////////////////////
   void update_in_neighbors(void) {
      typedef std::pair<unsigned int, unsigned int> EDType;
      typedef std::vector<EDType> EdgeList;
      _max_degree = 0;
      EdgeDataType _max_weight = 0;
      std::vector<EdgeList> in_edges(_num_nodes);
      for (unsigned int node = 0; node < _num_nodes; ++node) {
         for (unsigned int nbr = outgoing_index()[node]; nbr < outgoing_index()[node + 1]; ++nbr) {
            unsigned int nbr_id = out_neighbors()[nbr];
            _max_weight = std::max(_max_weight, out_edge_data()[nbr]);
            in_edges[nbr_id].push_back(EDType(node, out_edge_data()[nbr]));
         }
         _max_degree = std::max(_max_degree, outgoing_index()[node + 1] - outgoing_index()[node]);
      }
      unsigned int count = 0;
      for (unsigned int node = 0; node < _num_nodes; ++node) {
         incoming_index()[node] = count;
         for (EdgeList::iterator it = in_edges[node].begin(); it != in_edges[node].end(); ++it) {
            in_neighbors()[count] = (*it).first;
            in_edge_data()[count] = (*it).second;
            count++;
         }
      }
      incoming_index()[_num_nodes] = count;
//      std::cout << "MaxDegree[" << _max_degree << "]. , Max weight:: " << _max_weight << " ";
      fprintf(stderr, "MaxDegree[%d]. , Max weight:: %d ",(int)_max_degree ,(int)_max_weight);;
   }
/////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////
   void print_compact(void) {
      std::cout << "\nOut-index [";
      for (size_t i = 0; i < _num_nodes + 1; ++i) {
         std::cout << " " << outgoing_index()[i] << ",";
      }
      std::cout << "]\nNeigh[";
      for (size_t i = 0; i < _num_edges; ++i) {
         std::cout << " " << out_neighbors()[i] << ",";
      }
      std::cout << "]\nEData [";
      for (size_t i = 0; i < _num_edges; ++i) {
         std::cout << " " << out_edge_data()[i] << ",";
      }
      std::cout << "\n]In-index [";
      for (size_t i = 0; i < _num_nodes + 1; ++i) {
         std::cout << " " << incoming_index()[i] << ",";
      }
      std::cout << "]\nIn-Neigh[";
      for (size_t i = 0; i < _num_edges; ++i) {
         std::cout << " " << in_neighbors()[i] << ",";
      }
      std::cout << "]\nIn-EData [";
      for (size_t i = 0; i < _num_edges; ++i) {
         std::cout << " " << in_edge_data()[i] << ",";
      }
      std::cout << "]";
   }
/////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////
   unsigned int verify() {
      NodeDataTy * t_node_data = getData();
      unsigned int * t_incoming_index = outgoing_index();
      unsigned int * t_neighbors = out_neighbors();
      EdgeDataType * t_in_edge_data = out_edge_data();
      unsigned int err_count = 0;
      for (unsigned int node_id = 0; node_id < _num_nodes; ++node_id) {
         NodeDataTy curr_distance = t_node_data[node_id];
         //Go over all the neighbors.
         for (unsigned int idx = t_incoming_index[node_id]; idx < t_incoming_index[node_id + 1]; ++idx) {
            NodeDataTy temp = t_node_data[t_neighbors[idx]];
            if (curr_distance + t_in_edge_data[idx] < temp) {
               if (err_count < 10) {
                  std::cout << "Error :: ";
                  print_node(node_id);
                  std::cout << " With :: ";
                  print_node(t_neighbors[idx]);
                  std::cout << "\n";
               }
               err_count++;
            }
         }
      } //End for
      return err_count;
   }
   ////////////##############################################################///////////
   ////////////##############################################################///////////
   unsigned int verify_in() {
      unsigned int * t_node_data = getData();
      unsigned int * t_incoming_index = incoming_index();
      unsigned int * t_neighbors = in_neighbors();
      unsigned int * t_in_edge_data = in_edge_data();
      unsigned int err_count = 0;
      for (unsigned int node_id = 0; node_id < _num_nodes; ++node_id) {
         unsigned int curr_distance = t_node_data[node_id];
         //Go over all the neighbors.
         for (unsigned int idx = t_incoming_index[node_id]; idx < t_incoming_index[node_id + 1]; ++idx) {
            unsigned int temp = t_node_data[t_neighbors[idx]] + t_in_edge_data[idx];
            if (curr_distance > temp) {
               if (err_count < 10) {
                  std::cout << "Error :: ";
                  print_node(node_id);
                  std::cout << " With :: ";
                  print_node(t_neighbors[idx]);
                  std::cout << "\n";
               }
               err_count++;
            }
         }
      } //End for
      return err_count;
   }
/////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////
   void deallocate(void) {
      delete gpu_graph;
   }
};
//End LC_Graph
/////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////
}
} //End namespaces
#endif /* LC_LinearArray_Graph_H_ */
