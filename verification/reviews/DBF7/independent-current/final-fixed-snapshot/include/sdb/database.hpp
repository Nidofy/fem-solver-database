#pragma once
#include "checkpoint.hpp"
#include "implicit.hpp"

namespace sdb {
// Convenience owner. Individual modules can also be used independently.
// Model remains immutable; phases borrow views from state/contact.
template <class T> class Database {
    MeshDB<T> model_;
    StateDB<T> state_;
    ContactDB<T> contact_;
    std::unique_ptr<ImplicitExtension<T>> implicit_;

  public:
    explicit Database(MeshDB<T> &&model, bool rotations = false, std::size_t contact_capacity = 0)
        : model_(std::move(model)), state_(model_, rotations), contact_(contact_capacity) {
        contact_.import_surfaces(model_);
    }
    const MeshDB<T> &model() const noexcept { return model_; }
    StateDB<T> &state() noexcept { return state_; }
    const StateDB<T> &state() const noexcept { return state_; }
    ContactDB<T> &contact() noexcept { return contact_; }
    const ContactDB<T> &contact() const noexcept { return contact_; }
    ImplicitExtension<T> &enable_implicit() {
        if (!implicit_)
            implicit_ = std::make_unique<ImplicitExtension<T>>(model_);
        return *implicit_;
    }
    bool has_implicit() const noexcept { return bool(implicit_); }
    void begin_trial() {
        if (state_.in_trial() || contact_.in_trial())
            throw std::logic_error("database trial already active");
        state_.begin_trial();
        try {
            contact_.begin_trial();
        } catch (...) {
            state_.rollback();
            throw;
        }
    }
    void commit() {
        if (!state_.in_trial() || !contact_.in_trial())
            throw std::logic_error("database trial phases differ");
        contact_.commit();
        state_.commit();
    }
    void rollback() {
        if (!state_.in_trial() || !contact_.in_trial())
            throw std::logic_error("database trial phases differ");
        contact_.rollback();
        state_.rollback();
    }
    void save(const std::filesystem::path &path) const { save_checkpoint(path, model_, state_, &contact_); }
    void restore(const std::filesystem::path &path) { load_checkpoint(path, model_, state_, &contact_); }
};
} // namespace sdb
