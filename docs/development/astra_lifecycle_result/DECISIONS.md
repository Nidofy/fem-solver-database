# Lifecycle hardening decisions

Base: 480da32a17e3b60e6694b95e5c9ec83618e5b64b. Owner: current primary Codex agent.
Branch: codex/astra-lifecycle-result-hardening. GLM B02c at b87fb53 is a separate,
unaccepted View benchmark candidate; it is not integrated. No active competing
Codex writer was listed. No C3D8 implementation was found in the accepted tree.

1. A1 preserves file schemas and existing force_evaluations() total-pass semantics.
   Add output_force_evaluations() to distinguish output from required solve work.
   ResultPublisher accepts const StateDB and evaluates into reusable derived-only
   buffers (3 scalars/node + 12 scalars/IP); no full state/history copy. The existing
   plan local-force buffer is reused synchronously. ResultFrame owns its arrays.
   No cache is assumed valid because callers can mutate StateDB through its public API.
   U-only/non-due requests do not allocate derived buffers. Linear material evaluation
   is stateless; future history-dependent kernels must provide an equally const evaluator.
2. Preserve B01 acceptance scope. Its read-only test omitted internal_force/stress/
   strain; the expanded regression reproduces that gap before the fix. This is a
   stricter contract, not a claim that prior independent review ran these new tests.
3. Native result schema remains v1 until an indexed format is actually implemented.
   Do not label queued/written bytes durable: C++ flush provides no power-loss proof.

4. AsyncResultWriter owns admitted frames, reserves snapshot capacities plus native encoding and validation scratch, and holds the charge through in-flight flush. Persistent model and producer-owned frames are excluded and documented. Single producer; only cancel may run from a control thread. Explicit close is required for success/error reporting. No compression dependency or durable claim. NativeResultStorage now holds a cooperative cross-process directory lease; stale crash leases require explicit operator recovery.

5. A4 adds an optional .sdbrst.result-link manifest without changing legacy model/result/restart schemas. It binds exact restart and result bytes, compiled model/configuration and global increment. Publish only after draining results; append validates inside the writer lease before any recovery truncation. The checksum is mismatch detection, not authentication. Unlinked legacy restart can branch to a new output; append is explicitly refused. Stream hashing is bounded-memory O(file size) until an indexed commit identity is available.
