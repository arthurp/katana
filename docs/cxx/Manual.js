var Manual =
[
    [ "User-level Galois Programs", "Manual.html#autotoc_md20", null ],
    [ "Library-level Galois Constructs", "Manual.html#autotoc_md21", null ],
    [ "Performance Tuning of Galois Programs", "Manual.html#autotoc_md22", null ],
    [ "Introduction", "introduction.html", [
      [ "Target Audience", "introduction.html#target_audience", null ],
      [ "Data Structures and Algorithms", "introduction.html#ds_and_algo", null ],
      [ "Galois Execution Model", "introduction.html#exec_model", null ],
      [ "Galois as a Library", "introduction.html#katana_as_lib", null ],
      [ "Rest of the Manual", "introduction.html#rest_of", null ]
    ] ],
    [ "Parallel Loops", "parallel_loops.html", [
      [ "Introduction", "parallel_loops.html#parallel_loop_intro", null ],
      [ "katana::do_all", "parallel_loops.html#katana_do_all_manual", [
        [ "Work Stealing", "parallel_loops.html#workstealing", null ]
      ] ],
      [ "katana::for_each", "parallel_loops.html#katana_for_each_manual", null ],
      [ "katana::on_each", "parallel_loops.html#katana_on_each_manual", null ],
      [ "Specialized Parallel Loops", "parallel_loops.html#special_loops", null ]
    ] ],
    [ "Reduction Operations", "reduction.html", [
      [ "Introduction", "reduction.html#reduction_intro", null ],
      [ "Scalar Reduction", "reduction.html#scalar-reduction", [
        [ "Defining a Reducer", "reduction.html#scalar-define", null ],
        [ "Reducing Values", "reduction.html#scalar-reduce", null ],
        [ "Example", "reduction.html#scalar-example", null ]
      ] ]
    ] ],
    [ "Scheduler", "scheduler.html", [
      [ "Introduction", "scheduler.html#wl_intro", null ],
      [ "Chunked Worklists", "scheduler.html#chunked_wl", null ],
      [ "Ordered By Integer Metric (OBIM)", "scheduler.html#obim_wl", null ],
      [ "BulkSynchronous", "scheduler.html#bsp_wl", null ],
      [ "LocalQueue", "scheduler.html#lq_wl", null ],
      [ "OwnerComputes", "scheduler.html#onwer_wl", null ],
      [ "StableIterator", "scheduler.html#stable_iter_wl", null ],
      [ "Sequential Worklists", "scheduler.html#seq_wl", null ]
    ] ],
    [ "Concurrent Data Structures", "concurrent_data_structures.html", [
      [ "Parallel Graphs", "concurrent_data_structures.html#katana_graphs", [
        [ "Label-computation Graphs", "concurrent_data_structures.html#lc_graphs", [
          [ "Template Parameters", "concurrent_data_structures.html#lc_graph_tparam", null ],
          [ "APIs", "concurrent_data_structures.html#lc_graph_api", null ],
          [ "Different Storage Formats", "concurrent_data_structures.html#lc_graph_storage", null ],
          [ "Tracking Incoming Edges", "concurrent_data_structures.html#lc_graph_in_edges", null ]
        ] ],
        [ "Morph Graphs", "concurrent_data_structures.html#morph_graphs", [
          [ "Template Parameters", "concurrent_data_structures.html#morph_graph_tparam", null ],
          [ "APIs", "concurrent_data_structures.html#morph_graph_api", null ],
          [ "LC_Morph_Graph", "concurrent_data_structures.html#lc_morph_graph", null ]
        ] ]
      ] ],
      [ "Insert Bag", "concurrent_data_structures.html#insert_bag", null ]
    ] ],
    [ "Graph I/O and Utility Tools", "graph_io_util.html", [
      [ "Graph Input and Output", "graph_io_util.html#graphio", [
        [ "Reading Graphs", "graph_io_util.html#readwrite", null ],
        [ "Writing Graphs", "graph_io_util.html#writegraph", null ]
      ] ],
      [ "Utility Tools for Graphs", "graph_io_util.html#garph_utility", [
        [ "Tools to Convert Graphs Among Different Formats", "graph_io_util.html#graphconvert", null ],
        [ "Tools to Get Graph Statistics", "graph_io_util.html#graphstats", null ]
      ] ]
    ] ],
    [ "Output Statistics of Galois Programs", "output_stat.html", [
      [ "Basic Statistics", "output_stat.html#basic_stat", null ],
      [ "Advanced Control of Output Statistics", "output_stat.html#advanced_stat", null ],
      [ "Self-defined Statistics", "output_stat.html#self_stat", null ],
      [ "Timing Serial Code Region", "output_stat.html#stat_timer", null ]
    ] ],
    [ "NUMA-Awareness", "numa.html", [
      [ "What is NUMA?", "numa.html#numa-intro", null ],
      [ "NUMA Allocation Functions in Galois", "numa.html#numa-types", [
        [ "Local", "numa.html#numa-malloc-local", null ],
        [ "Floating", "numa.html#numa-malloc-floating", null ],
        [ "Blocked", "numa.html#numa-malloc-blocked", null ],
        [ "Interleaved", "numa.html#numa-malloc-interleaved", null ],
        [ "Specific", "numa.html#numa-malloc-specific", null ]
      ] ],
      [ "NUMA and Large Arrays", "numa.html#numa-large-array", null ],
      [ "NUMA Allocation in Galois Graphs", "numa.html#numa-galois-graphs", null ],
      [ "NUMA Guidelines", "numa.html#numa-best-behavior", null ]
    ] ],
    [ "Per-Thread and Per-Socket Storage", "thread_local_storage.html", [
      [ "Per-Thread Storage", "thread_local_storage.html#per_thread", null ],
      [ "Per-Socket Storage", "thread_local_storage.html#per_socket", null ]
    ] ],
    [ "Memory Allocators", "mem_allocator.html", [
      [ "The Need for Custom Memory Allocators", "mem_allocator.html#mem_allocator-intro", null ],
      [ "Memory Allocators in Galois", "mem_allocator.html#katana_alloc", [
        [ "Fixed Size Allocator", "mem_allocator.html#fixed-size-alloc", null ],
        [ "Per-iteration Allocator", "mem_allocator.html#per-iter-alloc", null ],
        [ "Power-of-2 Allocator", "mem_allocator.html#Pow2allocator", null ]
      ] ],
      [ "Low-level Memory Interface in Galois", "mem_allocator.html#low_level_mem", [
        [ "Types of Heaps in Galois", "mem_allocator.html#heaps_in_galois", null ],
        [ "Building Custom Allocators", "mem_allocator.html#build-custom-alloc", null ],
        [ "Plugging in 3rd Party Allocators", "mem_allocator.html#plugin-other-alloc", null ]
      ] ]
    ] ],
    [ "Build Your Own Galois Data Structure", "your_own_ds.html", [
      [ "Goal of this Section", "your_own_ds.html#goal_your_own_ds", null ],
      [ "Principles for NUMA-aware Data Structures", "your_own_ds.html#numa_aware_ds", [
        [ "Array-based Data Structures", "your_own_ds.html#array_like_ds", null ],
        [ "Per-thread Data Structure", "your_own_ds.html#per_thread_like_ds", null ],
        [ "Managing Memory with Custom Allocators", "your_own_ds.html#ds_and_mem_alloc", null ]
      ] ],
      [ "Conflict-aware Galois Data Structures", "your_own_ds.html#conflict_aware_ds", [
        [ "Conflict Awareness", "your_own_ds.html#torus_conflict_awareness", null ],
        [ "Container Interface", "your_own_ds.html#torus_stl", null ],
        [ "Easy Operator Cautiousness", "your_own_ds.html#torus_easy_cautiousness", null ],
        [ "Use the 2D Torus", "your_own_ds.html#torus_use", null ]
      ] ]
    ] ],
    [ "Profiling Galois Code", "profiling.html", [
      [ "Profiling with Intel VTune", "profiling.html#profile_w_vtune", null ],
      [ "Profiling with PAPI", "profiling.html#profile_w_papi", null ]
    ] ],
    [ "Performance Tuning", "tuning.html", null ]
];