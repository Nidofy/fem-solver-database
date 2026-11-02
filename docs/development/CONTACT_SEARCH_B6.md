# General contact search

GeneralContact retains the surface BVH partition across evaluations, including after
trial rollback. Each call validates the facets and recomputes their current boxes.
Reverse-order refit compares leaf bounds and propagates changes only through their
ancestors. Geometric bounds are refreshed from the current trial. The rounded-edge
search uses its separate per-call tree. Severe mesh rearrangement can reduce query
selectivity; correctness does not require rebalancing.

Whole-domain surface candidate counts and fills run per source node, with a serial
prefix sum that enforces the global capacity before candidate allocation. Worker
errors are rethrown in node order; valid witnesses retain canonical key ordering.
The extra traversal trades runtime for a hard allocation bound and deterministic writes.

GeneralContact::static_triangle_step_fraction takes predicted end coordinates and
returns the earliest positive-side stationary triangle crossing fraction across
eligible rule pairs. Call it before acceptance to limit or retry an increment;
starting penetration belongs to discrete contact. It is a conservative time limiter;
force scheduling still applies the normal opposition and penetration tests.
Quad, moving-facet and edge CCD remain outside this API.

Verification: general_contact_geometry_tests checks separated-to-contact refit,
serial/OpenMP equality, rollback/restore and a two-tetrahedron crossing at 1/3.
No production model or CUDA run. Benchmarking strongly deforming industrial
domains is pending.

Local isolated benchmark (benchmarks/bvh_refit.cpp, GCC 15.2 -O2): 32,768
distinct face boxes and 12 updates, changing roughly 1% per update. Total refit
was 6.00 ms vs 121.74 ms fresh build. Queries matched the fresh tree and the
known face ID. These are broadphase timings excluding contact force evaluation.
