#pragma once
#include "field.hpp"
#include "index.hpp"
#include "cpu_policy.hpp"
#include "batch_pages.hpp"

namespace sdb {
enum class TetKernel { Scalar, Batch8, Tiled8, Mixed8 };

// Immutable reference tables. Each work item owns one eight-element block.
template<class T, class Real=T> class TetBatch8 {
    static constexpr unsigned W=8;
    std::vector<Index> elements_, node_order_;
    BatchPages<T> displacement_;
    static Real narrow(T value) {
        if constexpr (!std::is_same_v<T,Real>)
            if (!std::isfinite(value) || std::abs(value)>std::numeric_limits<Real>::max())
                throw std::range_error("reference value outside mixed kernel precision");
        return static_cast<Real>(value);
    }
    Field<Index,4,AoSoA<W>> nodes_;
    Field<Real,12,AoSoA<W>> gradients_;
    Field<Real,3,AoSoA<W>> constants_; // volume, lambda, mu
  public:
    template<class Plan> explicit TetBatch8(const Plan& plan, bool tiled) {
        for(Index e=0;e<plan.is_tet.size();++e)
            if(plan.is_tet[e]) elements_.push_back(e);
        std::vector<Index> inverse;
        if(tiled) {
            auto nodes=plan.nodes.read();
            // Coarse element buckets retain canonical order within each neighborhood.
            auto tile=[&](Index e) {
                std::uint64_t sum=0;
                for(unsigned c=0;c<4;++c) sum+=nodes(e,c)/4;
                return sum/256;
            };
            std::stable_sort(elements_.begin(),elements_.end(),[&](Index a,Index b){return tile(a)<tile(b);});
            inverse.assign(plan.fixed.size(),invalid_index);
            for(auto e:elements_) for(unsigned a=0;a<4;++a) {
                Index global=nodes(e,a);
                if(inverse[global]==invalid_index) {
                    inverse[global]=index_of(node_order_.size());
                    node_order_.push_back(global);
                }
            }
            displacement_.allocate(product(node_order_.size(),3));
        }
        nodes_.resize(elements_.size()); gradients_.resize(elements_.size()); constants_.resize(elements_.size());
        auto n=nodes_.write(); auto g=gradients_.write(); auto p=constants_.write();
        for(std::size_t j=0;j<elements_.size();++j) {
            auto e=elements_[j];
            for(unsigned a=0;a<4;++a) n(j,a)=tiled?inverse[plan.nodes.read()(e,a)]:plan.nodes.read()(e,a);
            for(unsigned c=0;c<12;++c) g(j,c)=narrow(plan.gradients.read()(e,c));
            p(j,0)=narrow(plan.volume.read()(e)); p(j,1)=narrow(plan.lambda.read()(e)); p(j,2)=narrow(plan.mu.read()(e));
        }
    }
    const char* page_mode() const {return displacement_.mode();}
    void evaluate(CpuPolicy policy, const std::vector<std::size_t>& ips, View<const T,3> u,
                  View<const std::uint8_t> active, View<T,12> local, View<T,6> stress, View<T,6> strain) {
        if (!node_order_.empty()) {
            View<T,3> staged{displacement_.data(),node_order_.size()};
            parallel_for(node_order_.size(),policy,[&](std::size_t n){
                for(unsigned c=0;c<3;++c) staged(n,c)=u(node_order_[n],c);
            });
            u={staged.data,staged.count};
        }
        const auto nodes=nodes_.read(); const auto grad=gradients_.read(); const auto props=constants_.read();
        std::atomic<bool> overflow{false};
        parallel_for((elements_.size()+W-1)/W,policy,[&](std::size_t block){
            const auto first=block*W, count=std::min<std::size_t>(W,elements_.size()-first);
            alignas(64) Real du[3][3][W]{}, x[3][W]{}, s[6][W], eps[6][W], f[12][W];
            for(unsigned a=0;a<4;++a) {
                for(unsigned k=0;k<count;++k)
                    for(unsigned i=0;i<3;++i) {
                        T value=u(nodes(first+k,a),i);
                        if constexpr (!std::is_same_v<T,Real>) {
                            if (!active(elements_[first+k])) value=0;
                            if (!std::isfinite(value) || std::abs(value)>std::numeric_limits<Real>::max()) {
                                overflow.store(true,std::memory_order_relaxed);
                                value=0;
                            }
                        }
                        x[i][k]=static_cast<Real>(value);
                    }
                for(unsigned i=0;i<3;++i)
                    for(unsigned j=0;j<3;++j) {
                        const auto* g=grad.data+(block*12+3*a+j)*W;
#pragma omp simd
                        for(unsigned k=0;k<W;++k) du[i][j][k]+=x[i][k]*g[k];
                    }
            }
            const auto *v=props.data+block*3*W, *la=v+W, *mu=la+W;
#pragma omp simd
            for(unsigned k=0;k<W;++k) {
                eps[0][k]=du[0][0][k]; eps[1][k]=du[1][1][k]; eps[2][k]=du[2][2][k];
                eps[3][k]=Real(.5)*(du[0][1][k]+du[1][0][k]);
                eps[4][k]=Real(.5)*(du[1][2][k]+du[2][1][k]);
                eps[5][k]=Real(.5)*(du[2][0][k]+du[0][2][k]);
                Real trace=eps[0][k]+eps[1][k]+eps[2][k];
                for(unsigned c=0;c<6;++c) s[c][k]=2*mu[k]*eps[c][k]+(c<3?la[k]*trace:Real(0));
            }
            for(unsigned a=0;a<4;++a) {
                const auto *gx=grad.data+(block*12+3*a)*W, *gy=gx+W, *gz=gy+W;
#pragma omp simd
                for(unsigned k=0;k<W;++k) {
                    f[3*a][k]=v[k]*(s[0][k]*gx[k]+s[3][k]*gy[k]+s[5][k]*gz[k]);
                    f[3*a+1][k]=v[k]*(s[3][k]*gx[k]+s[1][k]*gy[k]+s[4][k]*gz[k]);
                    f[3*a+2][k]=v[k]*(s[5][k]*gx[k]+s[4][k]*gy[k]+s[2][k]*gz[k]);
                }
            }
            for(unsigned k=0;k<count;++k) {
                auto e=elements_[first+k]; auto ip=ips[e]; bool on=active(e)!=0;
                for(unsigned c=0;c<12;++c) {
                    if constexpr (!std::is_same_v<T,Real>)
                        if(on && !std::isfinite(f[c][k])) overflow.store(true,std::memory_order_relaxed);
                    local(e,c)=on?f[c][k]:T(0);
                }
                for(unsigned c=0;c<6;++c) {
                    strain(ip,c)=on?eps[c][k]:T(0); stress(ip,c)=on?s[c][k]:T(0);
                }
            }
        });
        if (overflow.load(std::memory_order_relaxed)) throw std::range_error("nonfinite mixed TET kernel");
    }
};
} // namespace sdb
