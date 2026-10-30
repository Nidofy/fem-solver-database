#pragma once
#include "state.hpp"
#include "io/native.hpp"
#include "dof_space.hpp"

namespace sdb {
class DofMap {
    std::vector<std::size_t> offsets_{0};
    std::vector<std::int64_t> equations_;
    std::size_t free_ = 0;
    std::uint64_t model_id_ = 0;

  public:
    template <class T> explicit DofMap(const MeshDB<T> &mesh) {
        model_id_=mesh.fingerprint();
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
    std::int64_t equation(NodeId node, Dof component) const {
        return equation(node.value(), static_cast<unsigned>(component));
    }
    template<class T> DofSpace contract(const MeshDB<T> &mesh) const {
        if(mesh.fingerprint()!=model_id_) throw std::invalid_argument("mechanical DOF model mismatch");
        DofSpaceData d; d.model=mesh.fingerprint(); d.nodes=mesh.nodes(); d.elements=mesh.elements();
        d.variables={{0,3,DofQuantity::Translation,"displacement","UNSPECIFIED","GLOBAL"},
                     {1,3,DofQuantity::Rotation,"rotation","UNSPECIFIED","GLOBAL"}};
        d.external_equations.resize(free_); std::iota(d.external_equations.begin(),d.external_equations.end(),0);
        d.solver_to_canonical=d.external_equations;
        if(offsets_.size()!=sum(mesh.nodes(),1)) throw std::invalid_argument("mechanical DOF mesh shape mismatch");
        std::map<std::pair<Index,unsigned>,double> prescribed;
        for(const auto &c:mesh.constraints()) prescribed[{c.node,c.component}]=c.value;
        for(Index node=0;node<mesh.nodes();++node) for(unsigned c=0;c<mesh.dofs(node);++c) {
            auto eq=equation(node,c); DofBinding b;
            b.key={DofOwner::Node,node,0,c/3,c%3};
            if(eq<0) { b.status=DofStatus::Prescribed; b.value=prescribed.at({node,c}); }
            else { b.status=DofStatus::Free; b.equation=static_cast<std::uint64_t>(eq); }
            d.bindings.push_back(std::move(b));
        }
        return DofSpace(std::move(d));
    }
};
// B=1 is CSR, B>1 stores dense row-major blocks. Input columns sorted and unique.
enum class MatrixSymmetry : std::uint8_t { General, Symmetric };
template <class T> struct MatrixData {
    std::uint32_t schema = 1, index_bits = 32, block_size = 1;
    ScalarType scalar = scalar_type<T>();
    std::uint64_t block_rows = 0;
    MatrixSymmetry symmetry = MatrixSymmetry::General;
    std::vector<std::uint64_t> offsets;
    std::vector<Index> columns;
    std::vector<T> values;
    template <class F> void visit(F f) {
        f(schema, index_bits, block_size, scalar, block_rows, symmetry, offsets, columns, values);
    }
};
template <class T, std::size_t B = 1> class SparseMatrix {
    static_assert(B > 0 && B < invalid_index);
    std::size_t rows_;
    std::vector<std::size_t> offsets_;
    std::vector<Index> columns_;
    AlignedVector<T> values_;
    MatrixSymmetry symmetry_ = MatrixSymmetry::General;

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
    std::size_t block_rows() const noexcept { return rows_; }
    std::span<const std::size_t> offsets() const { return offsets_; }
    std::span<const Index> columns() const { return columns_; }
    MatrixSymmetry symmetry() const noexcept { return symmetry_; }
    void validate_values(MatrixSymmetry symmetry) const {
        if (!enum_at_most(symmetry, MatrixSymmetry::Symmetric))
            throw std::invalid_argument("unknown matrix symmetry");
        for (auto v : values_) if (!std::isfinite(v)) throw std::invalid_argument("nonfinite matrix value");
        if (symmetry == MatrixSymmetry::General) return;
        for (std::size_t r = 0; r < rows_; ++r)
            for (auto j = offsets_[r]; j < offsets_[r + 1]; ++j) {
                auto c = columns_[j];
                auto lo = columns_.begin() + offsets_[c], hi = columns_.begin() + offsets_[c + 1];
                auto it = std::lower_bound(lo, hi, static_cast<Index>(r));
                if (it == hi || *it != r) throw std::invalid_argument("asymmetric matrix structure");
                auto k = static_cast<std::size_t>(it - columns_.begin());
                for (std::size_t a = 0; a < B; ++a)
                    for (std::size_t b = 0; b < B; ++b)
                        if (values_[j * B * B + a * B + b] != values_[k * B * B + b * B + a])
                            throw std::invalid_argument("asymmetric matrix values");
            }
    }
    void set_symmetry(MatrixSymmetry symmetry) { validate_values(symmetry); symmetry_ = symmetry; }
    MatrixData<T> data() const {
        validate_values(symmetry_);
        MatrixData<T> d;
        d.block_size = B; d.block_rows = rows_; d.symmetry = symmetry_;
        d.offsets.assign(offsets_.begin(), offsets_.end());
        d.columns = columns_; d.values.assign(values_.begin(), values_.end());
        return d;
    }
    static SparseMatrix from_data(const MatrixData<T> &d,
                                  std::uint64_t budget = std::numeric_limits<std::uint64_t>::max()) {
        if (d.schema != 1 || d.index_bits != 32 || d.block_size != B || d.scalar != scalar_type<T>() ||
            d.block_rows >= invalid_index || d.block_rows > std::numeric_limits<std::size_t>::max())
            throw std::invalid_argument("matrix schema/dimension mismatch");
        // Reject malformed value counts before the constructor allocates B*B
        // scalars per column. A tiny wire payload must not expand into an empty
        // or truncated matrix's claimed backing allocation.
        if (d.values.size() != product(d.columns.size(), product(B, B)))
            throw std::invalid_argument("matrix value shape");
        const auto backing_bytes = sum(product(d.offsets.size(), sizeof(std::size_t)),
            sum(product(d.columns.size(), sizeof(Index)), product(d.values.size(), sizeof(T))));
        if (backing_bytes > budget) throw std::length_error("matrix reconstruction budget");
        std::vector<std::size_t> offsets;
        offsets.reserve(d.offsets.size());
        for (auto v : d.offsets) {
            if (v > std::numeric_limits<std::size_t>::max()) throw std::length_error("matrix offset overflow");
            offsets.push_back(static_cast<std::size_t>(v));
        }
        SparseMatrix out(static_cast<std::size_t>(d.block_rows), std::move(offsets), d.columns);
        if (d.values.size() != out.values_.size()) throw std::invalid_argument("matrix value shape");
        std::copy(d.values.begin(), d.values.end(), out.values_.begin());
        out.set_symmetry(d.symmetry);
        return out;
    }
    auto encode(std::uint64_t budget = lifecycle::native::default_limit) const {
        auto bytes = sum(52, sum(product(offsets_.size(), 8),
            sum(product(columns_.size(), 4), product(values_.size(), sizeof(T)))));
        if (bytes > budget) throw std::length_error("matrix encoding budget");
        return lifecycle::native::pack(data(), budget);
    }
    static SparseMatrix decode(std::span<const std::uint8_t> bytes,
                               std::uint64_t budget = lifecycle::native::default_limit) {
        return from_data(lifecycle::native::unpack<MatrixData<T>>(bytes, budget), budget);
    }
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
