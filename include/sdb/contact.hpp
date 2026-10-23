#pragma once
#include "model.hpp"
#include <mutex>
#include <optional>

namespace sdb {
struct ContactKey {
    FaceId a, b;
    std::uint32_t feature_a = 0, feature_b = 0;
    auto operator<=>(const ContactKey &) const = default;
};
struct ContactHandle {
    Index slot = invalid_index;
    std::uint64_t generation = 0;
};
struct FaceTable {
    std::vector<FaceId> identities;
    std::vector<Id> surface_ids;
    AlignedVector<std::size_t> offsets{0};
    AlignedVector<Index> nodes;
    template <class T> explicit FaceTable(const MeshDB<T> &mesh) {
        constexpr unsigned tet[4][3] = {{0, 2, 1}, {0, 1, 3}, {1, 2, 3}, {2, 0, 3}};
        constexpr unsigned hex[6][4] = {{0, 3, 2, 1}, {4, 5, 6, 7}, {0, 1, 5, 4},
                                        {1, 2, 6, 5}, {2, 3, 7, 6}, {3, 0, 4, 7}};
        for (const auto &surface : mesh.surfaces())
            for (auto face : surface.faces) {
                mesh.validate_face(face);
                auto e = mesh.element_index(face.element);
                auto conn = mesh.connectivity(e);
                auto top = mesh.element(e).topology;
                std::vector<Index> local;
                if (top == Topology::Tet4)
                    for (unsigned k = 0; k < 3; ++k)
                        local.push_back(conn[tet[face.local][k]]);
                else if (top == Topology::Hex8)
                    for (unsigned k = 0; k < 4; ++k)
                        local.push_back(conn[hex[face.local][k]]);
                else
                    local.assign(conn.begin(), conn.end());
                if (face.side < 0)
                    std::reverse(local.begin(), local.end());
                identities.push_back(face);
                surface_ids.push_back(surface.id);
                nodes.insert(nodes.end(), local.begin(), local.end());
                offsets.push_back(nodes.size());
            }
    }
};
template <class T> struct ContactHistory {
    ContactKey key;
    T tangential_slip[3]{}, normal[3]{}, traction[3]{};
    T gap = 0;
    std::uint64_t generation = 1, last_seen = 0;
    bool active = true;
};
template <class T> class CandidateBuffer {
    static_assert(std::is_trivially_copyable_v<T>);
    AlignedVector<T> entries_;
    alignas(64) std::atomic<std::size_t> count_{0};
    alignas(64) std::atomic<std::size_t> dropped_{0};

  public:
    explicit CandidateBuffer(std::size_t capacity) : entries_(capacity) {}
    // Producers may append concurrently. Consumer/reset/reserve require joined producers.
    bool append(const T &entry) noexcept { return append_batch({&entry, 1}); }
    // All-or-nothing batch reservation amortizes contention. Source must not alias entries_.
    bool append_batch(std::span<const T> batch) noexcept {
        auto slot = count_.load(std::memory_order_relaxed);
        do {
            if (batch.size() > entries_.size() - slot) {
                dropped_.fetch_add(batch.size(), std::memory_order_relaxed);
                return false;
            }
        } while (!count_.compare_exchange_weak(slot, slot + batch.size(), std::memory_order_relaxed));
        for (std::size_t i = 0; i < batch.size(); ++i)
            entries_[slot + i] = batch[i];
        return true;
    }
    std::span<const T> entries() const { return {entries_.data(), count_.load()}; }
    void reset() noexcept {
        count_ = 0;
        dropped_ = 0;
    }
    void reserve(std::size_t capacity) {
        if (capacity < count_.load())
            throw std::length_error("candidate capacity");
        entries_.resize(capacity);
    }
    std::size_t capacity() const noexcept { return entries_.size(); }
    std::size_t dropped() const noexcept { return dropped_.load(); }
};
template <class T> class ContactDB {
    mutable std::mutex mutex_;
    std::vector<Surface> surfaces_;
    std::vector<ContactHistory<T>> slots_;
    std::vector<Index> free_;
    std::map<ContactKey, Index> lookup_;
    std::optional<std::vector<ContactHistory<T>>> backup_;
    std::uint64_t next_generation_ = 1;

  public:
    CandidateBuffer<ContactKey> candidates;
    explicit ContactDB(std::size_t capacity = 0) : candidates(capacity) {
        slots_.reserve(capacity);
        free_.reserve(capacity);
    }
    template <class R> void import_surfaces(const MeshDB<R> &model) {
        std::lock_guard lock(mutex_);
        surfaces_ = model.surfaces();
    }
    std::vector<Surface> surfaces() const {
        std::lock_guard lock(mutex_);
        return surfaces_;
    }
    ContactHandle touch(ContactKey key, std::uint64_t step) {
        std::lock_guard lock(mutex_);
        if (auto it = lookup_.find(key); it != lookup_.end()) {
            auto &h = slots_[it->second];
            h.last_seen = step;
            return {it->second, h.generation};
        }
        if (next_generation_ == std::numeric_limits<std::uint64_t>::max())
            throw std::overflow_error("contact generation exhausted");
        Index i;
        if (free_.empty()) {
            i = index_of(slots_.size());
            if (free_.capacity() < slots_.size() + 1)
                free_.reserve(std::max<std::size_t>(slots_.size() + 1, free_.capacity() * 2));
            slots_.push_back({});
        } else {
            i = free_.back();
            free_.pop_back();
        }
        auto &h = slots_[i];
        h = {};
        h.generation = next_generation_++;
        h.key = key;
        h.last_seen = step;
        try {
            lookup_.emplace(key, i);
        } catch (...) {
            h.active = false;
            free_.push_back(i);
            throw;
        }
        return {i, h.generation};
    }
    ContactHistory<T> get(ContactHandle handle) const {
        std::lock_guard lock(mutex_);
        const auto &h = slots_.at(handle.slot);
        if (!h.active || h.generation != handle.generation)
            throw std::logic_error("stale contact handle");
        return h;
    }
    void update(ContactHandle handle, const ContactHistory<T> &value) {
        std::lock_guard lock(mutex_);
        auto &h = slots_.at(handle.slot);
        if (!h.active || h.generation != handle.generation || value.key != h.key ||
            value.generation != h.generation || !value.active)
            throw std::logic_error("contact identity mismatch");
        h = value;
    }
    std::size_t prune(std::uint64_t oldest_step) {
        std::lock_guard lock(mutex_);
        std::size_t removed = 0;
        free_.reserve(slots_.size());
        for (Index i = 0; i < slots_.size(); ++i) {
            auto &h = slots_[i];
            if (h.active && h.last_seen < oldest_step) {
                if (h.generation == std::numeric_limits<std::uint64_t>::max())
                    throw std::overflow_error("contact generation exhausted");
                lookup_.erase(h.key);
                h.active = false;
                ++h.generation;
                free_.push_back(i);
                ++removed;
            }
        }
        return removed;
    }
    std::vector<ContactHistory<T>> snapshot() const {
        std::lock_guard lock(mutex_);
        return slots_;
    }
    void restore(std::vector<ContactHistory<T>> values,
                 std::optional<std::vector<Surface>> surfaces = std::nullopt) {
        std::map<ContactKey, Index> lookup;
        std::vector<Index> free;
        index_of(values.size());
        for (Index i = 0; i < values.size(); ++i) {
            const auto &h = values[i];
            if (!h.generation || !std::isfinite(h.gap))
                throw std::invalid_argument("invalid contact history");
            for (int c = 0; c < 3; ++c)
                if (!std::isfinite(h.normal[c]) || !std::isfinite(h.traction[c]) ||
                    !std::isfinite(h.tangential_slip[c]))
                    throw std::invalid_argument("nonfinite contact history");
            if (h.active) {
                if (!lookup.emplace(h.key, i).second)
                    throw std::invalid_argument("duplicate contact history");
            } else
                free.push_back(i);
        }
        std::lock_guard lock(mutex_);
        if (backup_)
            throw std::logic_error("restore during trial");
        for (const auto &h : values) {
            if (h.generation == std::numeric_limits<std::uint64_t>::max())
                throw std::invalid_argument("contact generation exhausted");
            next_generation_ = std::max(next_generation_, h.generation + 1);
        }
        slots_.swap(values);
        lookup_.swap(lookup);
        free_.swap(free);
        if (surfaces)
            surfaces_.swap(*surfaces);
    }
    void begin_trial() {
        std::lock_guard lock(mutex_);
        if (backup_)
            throw std::logic_error("contact trial active");
        backup_ = slots_;
    }
    void commit() {
        std::lock_guard lock(mutex_);
        if (!backup_)
            throw std::logic_error("no contact trial");
        backup_.reset();
    }
    void rollback() {
        std::lock_guard lock(mutex_);
        if (!backup_)
            throw std::logic_error("no contact trial");
        std::map<ContactKey, Index> lookup;
        std::vector<Index> free;
        free.reserve(backup_->size());
        for (Index i = 0; i < backup_->size(); ++i) {
            const auto &h = (*backup_)[i];
            if (h.active)
                lookup.emplace(h.key, i);
            else
                free.push_back(i);
        }
        slots_.swap(*backup_);
        lookup_.swap(lookup);
        free_.swap(free);
        backup_.reset();
    }
    bool in_trial() const {
        std::lock_guard lock(mutex_);
        return backup_.has_value();
    }
};
} // namespace sdb
