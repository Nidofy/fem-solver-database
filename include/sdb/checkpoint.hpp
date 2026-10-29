#pragma once
#include "contact.hpp"
#include "state.hpp"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <chrono>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#include <fcntl.h>
#endif

namespace sdb::checkpoint_detail {
using Bytes = std::vector<std::uint8_t>;
inline std::uint32_t crc(std::span<const std::uint8_t> bytes) {
    std::uint32_t c = 0xffffffffu;
    for (auto b : bytes) {
        c ^= b;
        for (int k = 0; k < 8; ++k)
            c = (c >> 1) ^ ((0u - (c & 1u)) & 0xedb88320u);
    }
    return ~c;
}
struct Writer {
    Bytes bytes;
    template <class U> void integer(U v) {
        static_assert(std::is_unsigned_v<U>);
        for (std::size_t k = 0; k < sizeof(U); ++k)
            bytes.push_back(static_cast<std::uint8_t>(v >> (8 * k)));
    }
    template <class T> void scalar(T value) {
        if constexpr (std::is_floating_point_v<T>) {
            if constexpr (sizeof(T) == 4)
                integer(std::bit_cast<std::uint32_t>(value));
            else
                integer(std::bit_cast<std::uint64_t>(value));
        } else
            integer(static_cast<std::make_unsigned_t<T>>(value));
    }
    void chunk(std::uint32_t id, const Writer &body) {
        integer(id);
        integer<std::uint64_t>(body.bytes.size());
        integer(crc(body.bytes));
        bytes.insert(bytes.end(), body.bytes.begin(), body.bytes.end());
    }
};
inline void stream_bytes(std::ostream &out, std::span<const std::uint8_t> bytes) {
    if (!out.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
        throw std::runtime_error("checkpoint stream write failed");
}
inline std::uint32_t crc_continue(std::uint32_t c, std::span<const std::uint8_t> bytes) {
    for (auto b : bytes) {
        c ^= b;
        for (int k = 0; k < 8; ++k)
            c = (c >> 1) ^ ((0u - (c & 1u)) & 0xedb88320u);
    }
    return c;
}
template <class F> void stream_field(std::ostream &out, std::uint32_t id, const F &field) {
    // Preserve exact v2 bytes. Two read-only field passes avoid a full-field staging allocation.
    auto encode = [&](auto sink) {
        Writer block;
        block.bytes.reserve(65536);
        block.integer<std::uint64_t>(field.size());
        block.integer<std::uint32_t>(F::components);
        block.integer<std::uint8_t>(sizeof(typename F::value_type));
        auto v = field.read();
        for (std::size_t c = 0; c < F::components; ++c)
            for (std::size_t i = 0; i < field.size(); ++i) {
                block.scalar(v(i, c));
                if (block.bytes.size() >= 65520) {
                    sink(block.bytes);
                    block.bytes.clear();
                }
            }
        if (!block.bytes.empty())
            sink(block.bytes);
    };
    std::uint32_t checksum = 0xffffffffu;
    encode([&](const auto &bytes) { checksum = crc_continue(checksum, bytes); });
    Writer header;
    header.integer(id);
    header.integer<std::uint64_t>(
        13 + product(product(field.size(), F::components), sizeof(typename F::value_type)));
    header.integer(~checksum);
    stream_bytes(out, header.bytes);
    encode([&](const auto &bytes) { stream_bytes(out, bytes); });
}
struct Reader {
    std::span<const std::uint8_t> bytes;
    std::size_t pos = 0;
    template <class U> U integer() {
        static_assert(std::is_unsigned_v<U>);
        if (sizeof(U) > bytes.size() - pos)
            throw std::runtime_error("truncated checkpoint");
        U v = 0;
        for (std::size_t k = 0; k < sizeof(U); ++k)
            v |= static_cast<U>(bytes[pos++]) << (8 * k);
        return v;
    }
    template <class T> T scalar() {
        if constexpr (std::is_floating_point_v<T>) {
            if constexpr (sizeof(T) == 4)
                return std::bit_cast<T>(integer<std::uint32_t>());
            else
                return std::bit_cast<T>(integer<std::uint64_t>());
        } else
            return static_cast<T>(integer<std::make_unsigned_t<T>>());
    }
    Reader chunk(std::uint32_t id) {
        if (integer<std::uint32_t>() != id)
            throw std::runtime_error("checkpoint chunk order/type");
        auto n = integer<std::uint64_t>();
        auto checksum = integer<std::uint32_t>();
        if (n > bytes.size() - pos)
            throw std::runtime_error("truncated chunk");
        auto body = bytes.subspan(pos, static_cast<std::size_t>(n));
        pos += static_cast<std::size_t>(n);
        if (crc(body) != checksum)
            throw std::runtime_error("checkpoint CRC mismatch");
        return {body};
    }
    void end() const {
        if (pos != bytes.size())
            throw std::runtime_error("checkpoint trailing data");
    }
};
inline void face(Writer &w, FaceId f) {
    w.integer(f.element);
    w.integer(f.local);
    w.integer(static_cast<std::uint8_t>(f.side));
}

// Extension manifest is checked byte-for-byte before decoding any values. Required
// fields are bounded scalar blocks; Reconstruct/Omit retain declared initial data.
template<class Sink> void write_registered(const RegisteredState &state, Sink sink) {
    state.validate();
    const auto manifest = state.manifest(); sink(200u, manifest);
    for (std::size_t index=0; index<state.entries().size(); ++index) {
        const auto &entry = *state.entries()[index];
        if (entry.descriptor().restart != RestartPolicy::Required) continue;
        std::size_t offset = 0;
        do {
            auto count = std::min<std::size_t>(4096, entry.scalar_count()-offset);
            Writer block; block.integer<std::uint64_t>(index); block.integer<std::uint64_t>(offset);
            block.integer<std::uint64_t>(count);
            auto values = entry.block(offset,count); block.bytes.insert(block.bytes.end(),values.begin(),values.end());
            sink(201u,block.bytes); offset += count;
        } while (offset < entry.scalar_count());
    }
}
template<class Source> void read_registered(RegisteredState &state, Source source) {
    const auto manifest = state.manifest();
    auto actual = source(200u, manifest.size());
    if (actual.size() != manifest.size() || !std::equal(actual.begin(),actual.end(),manifest.begin()))
        throw std::runtime_error("registered state schema/initialization/layout mismatch");
    for (std::size_t index=0; index<state.entries().size(); ++index) {
        const auto &entry = *state.entries()[index];
        if (entry.descriptor().restart != RestartPolicy::Required) continue;
        std::size_t offset = 0;
        do {
            auto bytes = source(201u, 4096u*9u+24u); Reader block{bytes};
            if (block.integer<std::uint64_t>() != index || block.integer<std::uint64_t>() != offset)
                throw std::runtime_error("registered state field/offset mismatch");
            auto count = block.integer<std::uint64_t>();
            if (count > 4096 || count > entry.scalar_count()-offset || (offset<entry.scalar_count() && !count))
                throw std::runtime_error("registered state scalar block length");
            state.restore_block(index,block.bytes.subspan(block.pos),offset,static_cast<std::size_t>(count));
            offset += static_cast<std::size_t>(count);
        } while (offset < entry.scalar_count());
    }
    state.validate();
}
inline FaceId face(Reader &r) {
    FaceId f;
    f.element = r.integer<Id>();
    f.local = r.integer<std::uint16_t>();
    auto s = r.integer<std::uint8_t>();
    if (s != 1 && s != 255)
        throw std::runtime_error("invalid face side");
    f.side = s == 1 ? 1 : -1;
    return f;
}
template <class F> void atomic_stream_write(const std::filesystem::path &path, F fn) {
    static std::atomic<std::uint64_t> serial{0};
    auto temp = path;
    temp += ".tmp." + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "." +
            std::to_string(serial++);
    try {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out)
            throw std::runtime_error("cannot create native temporary file");
        fn(out);
        out.flush();
        if (!out)
            throw std::runtime_error("native flush failed");
        out.close();
        if (!out)
            throw std::runtime_error("native close failed");
#ifdef _WIN32
        auto handle = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE)
            throw std::runtime_error("native sync open failed");
        bool synced = FlushFileBuffers(handle) != 0;
        CloseHandle(handle);
        if (!synced)
            throw std::runtime_error("native sync failed");
        if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("native atomic replace failed");
#else
        int fd = ::open(temp.c_str(), O_RDONLY);
        if (fd < 0)
            throw std::runtime_error("native sync open failed");
        int rc = fsync(fd);
        ::close(fd);
        if (rc)
            throw std::runtime_error("native sync failed");
        std::filesystem::rename(temp, path);
        auto parent = path.parent_path();
        if (parent.empty())
            parent = ".";
        fd = ::open(parent.c_str(), O_RDONLY | O_DIRECTORY);
        if (fd < 0)
            throw std::runtime_error("native directory sync open failed");
        rc = fsync(fd);
        ::close(fd);
        if (rc)
            throw std::runtime_error("native directory sync failed");
#endif
    } catch (...) {
        std::error_code ec;
        std::filesystem::remove(temp, ec);
        throw;
    }
}

inline void atomic_write(const std::filesystem::path &path, std::span<const std::uint8_t> bytes) {
    static std::atomic<std::uint64_t> serial{0};
    auto temp = path;
    temp += ".tmp." + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "." +
            std::to_string(serial++);
    FILE *file = nullptr;
#ifdef _WIN32
#ifdef _MSC_VER
    _wfopen_s(&file, temp.c_str(), L"wbx");
#else
    file = _wfopen(temp.c_str(), L"wbx");
#endif
#else
    file = std::fopen(temp.c_str(), "wbx");
#endif
    if (!file)
        throw std::runtime_error("cannot create checkpoint temporary file");
    try {
        if (std::fwrite(bytes.data(), 1, bytes.size(), file) != bytes.size() || std::fflush(file) != 0)
            throw std::runtime_error("checkpoint write failed");
#ifdef _WIN32
        if (_commit(_fileno(file)) != 0)
            throw std::runtime_error("checkpoint flush failed");
#else
        if (fsync(fileno(file)) != 0)
            throw std::runtime_error("checkpoint flush failed");
#endif
        auto rc = std::fclose(file);
        file = nullptr;
        if (rc)
            throw std::runtime_error("checkpoint close failed");
#ifdef _WIN32
        if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("checkpoint atomic replace failed");
#else
        std::filesystem::rename(temp, path);
        auto parent = path.parent_path();
        if (parent.empty())
            parent = ".";
        int fd = open(parent.c_str(), O_RDONLY | O_DIRECTORY);
        if (fd >= 0) {
            int status = fsync(fd);
            close(fd);
            if (status)
                throw std::runtime_error("checkpoint directory flush failed");
        } else
            throw std::runtime_error("checkpoint directory open failed");
#endif
    } catch (...) {
        if (file)
            std::fclose(file);
        std::error_code ec;
        std::filesystem::remove(temp, ec);
        throw;
    }
}
} // namespace sdb::checkpoint_detail

namespace sdb {
template <class T>
void save_checkpoint(const std::filesystem::path &path, const MeshDB<T> &mesh, const StateDB<T> &state,
                     const ContactDB<T> *contact = nullptr) {
    using namespace checkpoint_detail;
    if (state.in_trial() || (contact && contact->in_trial()))
        throw std::logic_error("checkpoint requires committed phase");
    if (state.model_id() != mesh.fingerprint())
        throw std::invalid_argument("checkpoint model mismatch");
    state.validate_shape();
    if (!std::isfinite(state.data().time) || !std::isfinite(state.data().dt) ||
        state.data().time < 0 || state.data().dt < 0 || (state.data().step && state.data().dt == 0))
        throw std::invalid_argument("invalid checkpoint metadata");
    Writer file, header;
    file.integer<std::uint64_t>(0x325642444253ull); // S B D B V 2
    const bool extensions = state.registered_fields() && !state.registered_fields()->empty();
    header.integer<std::uint32_t>(extensions ? 3 : 2);
    header.integer<std::uint8_t>(sizeof(T));
    header.integer<std::uint8_t>(sizeof(Index));
    header.integer<std::uint16_t>(0x0102);
    header.integer(state.model_id());
    header.integer(state.data().step);
    header.scalar(state.data().time);
    header.scalar(state.data().dt);
    header.integer<std::uint8_t>(state.data().rotation.has_value());
    header.integer<std::uint8_t>(contact != nullptr);
    if (extensions) header.integer(state.registered_fields()->fingerprint());
    file.chunk(0, header);
    atomic_stream_write(path, [&](std::ostream &out) {
        stream_bytes(out, file.bytes);
        file.bytes.clear();
        std::uint32_t id = 1;
        state.data().fields([&](const auto &field) { stream_field(out, id++, field); });
        if (extensions) write_registered(*state.registered_fields(), [&](std::uint32_t kind, const auto &bytes) {
            Writer block; block.bytes.assign(bytes.begin(),bytes.end()); Writer record; record.chunk(kind,block);
            stream_bytes(out,record.bytes);
        });
        if (contact) {
            Writer block;
            auto history = contact->snapshot();
            block.integer<std::uint64_t>(history.size());
            for (const auto &h : history) {
                face(block, h.key.a);
                face(block, h.key.b);
                block.integer(h.key.feature_a);
                block.integer(h.key.feature_b);
                block.integer(h.generation);
                block.integer(h.last_seen);
                block.integer<std::uint8_t>(h.active);
                block.scalar(h.gap);
                for (int c = 0; c < 3; ++c) {
                    block.scalar(h.tangential_slip[c]);
                    block.scalar(h.normal[c]);
                    block.scalar(h.traction[c]);
                }
            }
            file.chunk(100, block);
        }
        Writer end;
        file.chunk(0xffffffffu, end);
        stream_bytes(out, file.bytes);
    });
}
template <class T>
void load_checkpoint(const std::filesystem::path &path, const MeshDB<T> &mesh, StateDB<T> &state,
                     std::type_identity_t<ContactDB<T> *> contact = nullptr,
                     std::size_t max_bytes = std::size_t(4) * 1024 * 1024 * 1024) {
    using namespace checkpoint_detail;
    if (state.in_trial() || (contact && contact->in_trial()))
        throw std::logic_error("restore requires committed phase");
    const auto length = std::filesystem::file_size(path);
    if (length > max_bytes || length > std::numeric_limits<std::size_t>::max())
        throw std::length_error("checkpoint size limit");
    Bytes bytes(static_cast<std::size_t>(length));
    std::ifstream input(path, std::ios::binary);
    if (!input.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
        throw std::runtime_error("checkpoint read failed");
    Reader file{bytes};
    if (file.integer<std::uint64_t>() != 0x325642444253ull)
        throw std::runtime_error("checkpoint magic");
    auto header = file.chunk(0);
    const auto schema = header.integer<std::uint32_t>();
    const bool extensions = state.registered_fields() && !state.registered_fields()->empty();
    if (schema != (extensions ? 3u : 2u) || header.integer<std::uint8_t>() != sizeof(T) ||
        header.integer<std::uint8_t>() != sizeof(Index) || header.integer<std::uint16_t>() != 0x0102)
        throw std::runtime_error("checkpoint format/precision/index/endian mismatch");
    if (header.integer<std::uint64_t>() != mesh.fingerprint() || state.model_id() != mesh.fingerprint())
        throw std::runtime_error("checkpoint model mismatch");
    const auto step = header.integer<std::uint64_t>();
    const auto time = header.scalar<double>(), dt = header.scalar<double>();
    const auto rotation = header.integer<std::uint8_t>(), contacts = header.integer<std::uint8_t>();
    if (extensions && header.integer<std::uint64_t>() != state.registered_fields()->fingerprint())
        throw std::runtime_error("checkpoint registered state identity mismatch");
    header.end();
    if (rotation > 1 || contacts > 1 || bool(contacts) != (contact != nullptr) || !std::isfinite(time) ||
        !std::isfinite(dt) || time < 0 || dt < 0 || (step && dt == 0))
        throw std::runtime_error("invalid checkpoint metadata");
    StateDB<T> restored(mesh, rotation != 0);
    restored.copy_registered_layout(state);
    auto &data = restored.data();
    data.step = step;
    data.time = time;
    data.dt = dt;
    std::uint32_t id = 1;
    data.fields([&](auto &field) {
        using F = std::decay_t<decltype(field)>;
        using V = typename F::value_type;
        auto block = file.chunk(id++);
        if (block.integer<std::uint64_t>() != field.size() ||
            block.integer<std::uint32_t>() != F::components || block.integer<std::uint8_t>() != sizeof(V))
            throw std::runtime_error("checkpoint field shape mismatch");
        auto v = field.write();
        for (std::size_t c = 0; c < F::components; ++c)
            for (std::size_t i = 0; i < field.size(); ++i) {
                V value = block.scalar<V>();
                if (!std::isfinite(value))
                    throw std::runtime_error("nonfinite checkpoint state");
                if constexpr (std::is_same_v<V, std::uint8_t>)
                    if (value > 1)
                        throw std::runtime_error("invalid activation flag");
                v(i, c) = value;
            }
        block.end();
    });
    if (extensions) read_registered(*data.registered, [&](std::uint32_t kind, std::size_t limit) {
        auto block = file.chunk(kind);
        if (block.bytes.size() > limit) throw std::length_error("checkpoint registered block budget");
        return block.bytes;
    });
    auto mass = data.mass.read(), inverse = data.inverse_mass.read();
    for (std::size_t i = 0; i < mass.count; ++i)
        if (mass(i) < 0 || inverse(i) < 0 ||
            (mass(i) == 0 ? inverse(i) != 0
                          : std::abs(mass(i) * inverse(i) - 1) > T(64) * std::numeric_limits<T>::epsilon()))
            throw std::runtime_error("checkpoint mass inconsistency");
    std::vector<ContactHistory<T>> history;
    if (contact) {
        auto block = file.chunk(100);
        auto n = block.integer<std::uint64_t>();
        constexpr std::size_t record_bytes = 47 + 10 * sizeof(T);
        if (n >= (invalid_index) || n > (block.bytes.size() - block.pos) / record_bytes)
            throw std::runtime_error("invalid contact count");
        history.resize(static_cast<std::size_t>(n));
        for (auto &h : history) {
            h.key.a = face(block);
            h.key.b = face(block);
            mesh.validate_face(h.key.a);
            mesh.validate_face(h.key.b);
            h.key.feature_a = block.integer<std::uint32_t>();
            h.key.feature_b = block.integer<std::uint32_t>();
            h.generation = block.integer<std::uint64_t>();
            h.last_seen = block.integer<std::uint64_t>();
            auto active = block.integer<std::uint8_t>();
            if (active > 1)
                throw std::runtime_error("invalid contact activity");
            h.active = active != 0;
            h.gap = block.scalar<T>();
            for (int c = 0; c < 3; ++c) {
                h.tangential_slip[c] = block.scalar<T>();
                h.normal[c] = block.scalar<T>();
                h.traction[c] = block.scalar<T>();
            }
        }
        block.end();
    }
    file.chunk(0xffffffffu).end();
    file.end();
    if (contact)
        contact->restore(std::move(history), mesh.surfaces());
    static_assert(std::is_nothrow_move_assignable_v<StateData<T>>);
    state.data() = std::move(restored.data());
}
} // namespace sdb
