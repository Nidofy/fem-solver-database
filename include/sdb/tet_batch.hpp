#pragma once
#include "field.hpp"
#include "index.hpp"
#include "cpu_policy.hpp"

namespace sdb {
enum class TetKernel { Scalar, Batch8 };

// Immutable reference tables. Each work item owns one eight-element block.
template<class T> class TetBatch8 {
    static constexpr unsigned W=8;
    std::vector<Index> elements_;
    Field<Index,4,AoSoA<W>> nodes_;
    Field<T,12,AoSoA<W>> gradients_;
    Field<T,3,AoSoA<W>> constants_; // volume, lambda, mu
  public:
    template<class Plan> explicit TetBatch8(const Plan& plan) {
        for(Index e=0;e<plan.is_tet.size();++e)
            if(plan.is_tet[e]) elements_.push_back(e);
        nodes_.resize(elements_.size()); gradients_.resize(elements_.size()); constants_.resize(elements_.size());
        auto n=nodes_.write(); auto g=gradients_.write(); auto p=constants_.write();
        for(std::size_t j=0;j<elements_.size();++j) {
            auto e=elements_[j];
            for(unsigned a=0;a<4;++a) n(j,a)=plan.nodes.read()(e,a);
            for(unsigned c=0;c<12;++c) g(j,c)=plan.gradients.read()(e,c);
            p(j,0)=plan.volume.read()(e); p(j,1)=plan.lambda.read()(e); p(j,2)=plan.mu.read()(e);
        }
    }
    void evaluate(CpuPolicy policy, const std::vector<std::size_t>& ips, View<const T,3> u,
                  View<const std::uint8_t> active, View<T,12> local, View<T,6> stress, View<T,6> strain) const {
        const auto nodes=nodes_.read(); const auto grad=gradients_.read(); const auto props=constants_.read();
        parallel_for((elements_.size()+W-1)/W,policy,[&](std::size_t block){
            const auto first=block*W, count=std::min<std::size_t>(W,elements_.size()-first);
            alignas(64) T du[3][3][W]{}, x[3][W]{}, s[6][W], eps[6][W], f[12][W];
            for(unsigned a=0;a<4;++a) {
                for(unsigned k=0;k<count;++k)
                    for(unsigned i=0;i<3;++i) x[i][k]=u(nodes(first+k,a),i);
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
                eps[3][k]=T(.5)*(du[0][1][k]+du[1][0][k]);
                eps[4][k]=T(.5)*(du[1][2][k]+du[2][1][k]);
                eps[5][k]=T(.5)*(du[2][0][k]+du[0][2][k]);
                T trace=eps[0][k]+eps[1][k]+eps[2][k];
                for(unsigned c=0;c<6;++c) s[c][k]=2*mu[k]*eps[c][k]+(c<3?la[k]*trace:T(0));
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
                for(unsigned c=0;c<12;++c) local(e,c)=on?f[c][k]:T(0);
                for(unsigned c=0;c<6;++c) {
                    strain(ip,c)=on?eps[c][k]:T(0); stress(ip,c)=on?s[c][k]:T(0);
                }
            }
        });
    }
};
} // namespace sdb
