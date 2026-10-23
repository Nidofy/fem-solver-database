#pragma once
#include "state.hpp"

namespace sdb {
class DofMap {
    std::vector<std::size_t> offsets_{0};
    std::vector<std::int64_t> equations_;
    std::size_t free_ = 0;

  public:
    template <class T> explicit DofMap(const MeshDB<T> &mesh) {
        for (Index n = 0; n < mesh.nodes(); ++n)
            offsets_.push_back(sum(offsets_.back(), mesh.dofs(n)));
        equations_.resize(offsets_.back(), 0);
        for (const auto &c : mesh.constraints())
            equations_[offsets_[c.node] + c.component] = -1;
        for (auto &e : equations_)
            if (e != -1)
                e = static_cast<std::int64_t>(free_++);
    }
    std::int64_t equation(Index n, unsigned c) const {
        if (n + std::size_t(1) >= offsets_.size() || c >= offsets_[n + 1] - offsets_[n])
            throw std::out_of_range("DOF index");
        return equations_[offsets_[n] + c];
    }
    std::size_t size() const { return free_; }
};
// B=1 is CSR, B>1 stores dense row-major blocks. Input columns sorted and unique.
template <class T, std::size_t B = 1> class SparseMatrix {
    static_assert(B > 0);
    std::size_t rows_;
    std::vector<std::size_t> offsets_;
    std::vector<Index> columns_;
    AlignedVector<T> values_;

  public:
    SparseMatrix(std::size_t rows, std::vector<std::size_t> offsets, std::vector<Index> columns)
        : rows_(rows), offsets_(std::move(offsets)), columns_(std::move(columns)) {
        index_of(rows);
        if (offsets_.size() != sum(rows, 1) || offsets_.front() != 0 || offsets_.back() != columns_.size())
            throw std::invalid_argument("invalid CSR offsets");
        for (std::size_t i = 0; i < rows; ++i) {
            if (offsets_[i] > offsets_[i + 1] || offsets_[i + 1] > columns_.size())
                throw std::invalid_argument("invalid CSR range");
            for (auto j = offsets_[i]; j < offsets_[i + 1]; ++j)
                if (columns_[j] >= rows || (j > offsets_[i] && columns_[j] <= columns_[j - 1]))
                    throw std::invalid_argument("invalid CSR columns");
        }
        values_.resize(product(columns_.size(), product(B, B)));
    }
    std::span<T> values() { return values_; }
    std::span<const T> values() const { return values_; }
    std::size_t size() const { return product(rows_, B); }
    void apply(std::span<const T> x, std::span<T> y) const {
        if (x.size() != size() || y.size() != size())
            throw std::invalid_argument("sparse vector dimension");
        if (!x.empty() && std::less<const T *>{}(x.data(), y.data() + y.size()) &&
            std::less<const T *>{}(y.data(), x.data() + x.size()))
            throw std::invalid_argument("sparse apply alias");
        for (std::size_t r = 0; r < rows_; ++r)
            for (std::size_t a = 0; a < B; ++a) {
                T v = 0;
                for (auto j = offsets_[r]; j < offsets_[r + 1]; ++j)
                    for (std::size_t b = 0; b < B; ++b)
                        v += values_[j * B * B + a * B + b] * x[columns_[j] * B + b];
                y[r * B + a] = v;
            }
    }
};
template <class T> struct ImplicitExtension {
    DofMap dofs;
    Field<T> residual;
    std::optional<SparseMatrix<T>> tangent;
    explicit ImplicitExtension(const MeshDB<T> &mesh) : dofs(mesh), residual(dofs.size()) {}
    template <class Operator>
    void matrix_free(Operator &&op, std::span<const T> input, std::span<T> output, const MeshDB<T> &mesh,
                     const StateDB<T> &state) const {
        if (input.size() != dofs.size() || output.size() != dofs.size())
            throw std::invalid_argument("matrix-free dimension");
        op(mesh, state, dofs, input, output);
    }
};
} // namespace sdb
