/** Galois Distributed Object Tracer -*- C++ -*-
 * @file
 * @section License
 *
 * Galois, a framework to exploit amorphous data-parallelism in irregular
 * programs.
 *
 * Copyright (C) 2012, The University of Texas at Austin. All rights reserved.
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
 * @author Andrew Lenharth <andrewl@lenharth.org>
 */

#ifndef GALOIS_RUNTIME_TRACER_H
#define GALOIS_RUNTIME_TRACER_H

#include <stdint.h>

namespace Galois {
namespace Runtime {

void trace_obj_send_impl(uint32_t owner, void* ptr, uint32_t remote);
void trace_obj_recv_impl(uint32_t owner, void* ptr);
void trace_bcast_recv_impl(uint32_t at);

//#define NOTRACE

inline void trace_obj_send(uint32_t owner, void* ptr, uint32_t remote) {
#ifndef NOTRACE
  trace_obj_send_impl(owner, ptr, remote);
#endif
}
inline void trace_obj_recv(uint32_t owner, void* ptr) {
#ifndef NOTRACE
  trace_obj_recv_impl(owner, ptr);
#endif
}
inline void trace_bcast_recv(uint32_t at) {
#ifdef NOTRACE
  trace_bcast_recv_impl(at);
#endif
}

}
}

#endif
