# Same-host CPU performance gate

On pull requests the workflow builds the base and merge candidate using the same
compiler, interleaves five runs of each with identical OpenMP settings, and
compares medians for the node view, TET gather/scatter, explicit steps and
end-to-end time. A regression above 30% or a nonzero hot allocation fails the
job. Raw samples are retained as workflow artifacts. The tolerance is a CI noise
allowance and can be overridden for a pinned host using tools/perf_gate.py.
Compare like-for-like workloads and machine/compiler settings. Locally the
66-row placement corpus passed self-comparison; a 50% slowdown was detected
and missing/undersampled cases were rejected. Hosted execution awaits a PR.
