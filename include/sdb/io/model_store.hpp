#pragma once
#include "native.hpp"
#include "../model_compiler/compiler.hpp"

namespace sdb::lifecycle {
inline constexpr std::uint64_t model_magic = 0x314c444d424453ull;
struct ModelManifest {
    std::uint32_t schema = 1, compiler_version = 1;
    std::uint8_t precision = 8;
    std::uint64_t mesh = 0, definition = 0;
    std::vector<SourceEntry> nodes, elements;
    template <class F> void visit(F f) {
        f(schema, compiler_version, precision, mesh, definition, nodes, elements);
    }
};
class ModelStore {
  public:
    template <class T> static std::uint64_t identity(const CompiledModel<T> &model) {
        return native::hash(native::pack(model.definition));
    }
    template <class T> static void save(const std::filesystem::path &path, const CompiledModel<T> &model) {
        auto bytes = native::pack(model.definition);
        ModelManifest manifest{model.definition.schema,
                               1,
                               sizeof(T),
                               model.mesh.fingerprint(),
                               native::hash(bytes),
                               model.sources.nodes,
                               model.sources.elements};
        native::atomic_stream(path, [&](auto &out) {
            native::prefix(out, model_magic);
            native::record(out, 0, native::pack(manifest));
            native::record(out, 1, bytes);
        });
    }
    template <class T> static CompiledModel<T> load(const std::filesystem::path &path) {
        std::ifstream in(path, std::ios::binary);
        native::prefix(in, model_magic);
        auto manifest = native::unpack<ModelManifest>(native::record(in, 0));
        if ((manifest.schema != 1 && manifest.schema != 2 && manifest.schema != 3) || manifest.compiler_version != 1 || manifest.precision != sizeof(T))
            throw std::runtime_error("model schema/compiler/precision mismatch");
        auto bytes = native::record(in, 1);
        native::end(in);
        if (native::hash(bytes) != manifest.definition)
            throw std::runtime_error("model definition identity mismatch");
        auto model = ModelCompiler{}.compile<T>(native::unpack<ModelIR>(bytes));
        if (model.definition.schema != manifest.schema || model.mesh.fingerprint() != manifest.mesh ||
            native::pack(model.sources.nodes) != native::pack(manifest.nodes) ||
            native::pack(model.sources.elements) != native::pack(manifest.elements))
            throw std::runtime_error("model reconstruction/source map mismatch");
        return model;
    }
};
} // namespace sdb::lifecycle
