from cython import final

from libc.stdint cimport uint64_t
from libcpp.memory cimport shared_ptr
from pyarrow.lib cimport Schema

from .cpp.libgalois.graphs.Graph cimport GraphTopology, RDGLoadOptions, _PropertyGraph


cdef _convert_string_list(l)

#
# Python Property Graph
#
cdef class PropertyGraphBase:
    cdef _PropertyGraph * underlying_property_graph(self) nogil except NULL

    @staticmethod
    cdef uint64_t _property_name_to_id(object prop, Schema schema) except -1

    @final
    cdef GraphTopology topology(PropertyGraphInterface)

    cpdef uint64_t num_nodes(PropertyGraphInterface)

    cpdef uint64_t num_edges(PropertyGraphInterface)

    cpdef uint64_t get_edge_dest(PropertyGraphInterface, uint64_t)

cdef class PropertyGraph(PropertyGraphBase):
    cdef:
        shared_ptr[_PropertyGraph] _underlying_property_graph

    cdef _PropertyGraph * underlying_property_graph(self) nogil except NULL

    @staticmethod
    cdef PropertyGraph make(shared_ptr[_PropertyGraph] u)
