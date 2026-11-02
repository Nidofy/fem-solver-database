# Material plugin ABI version 1

A provider exports a descriptor conforming to sdb/material_abi.h. The host
obtains the descriptor from a linked library or its own plugin loader, then
registers it in physics::MaterialRegistry. One registration binds algorithm,
precision, versioned schema ID, ordered scalar parameters/history and tangent
kind. The host copies names and retains the function pointer. Keep the module
loaded until all its evaluations finish. Registration is a setup operation.

Pass matching definition metadata, total tensor strain and committed history
to trial. The callback fills stress, trial history and optionally a 6x6
row-major tangent. Failed or incomplete evaluations discard output. Commit an
accepted trial by copying its history to the matching StateDB IP. Registry
calls do not mutate solver state. Concurrent evaluations require a reentrant
provider. Plugin loading, persistence of foreign code and automatic CpuRun
dispatch are outside this numerical ABI.
