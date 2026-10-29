#include <sdb/consumer_views.hpp>
#include <chrono>
#include <iostream>
#include <iomanip>
using namespace sdb;
using namespace sdb::consumer;
constexpr int corner[8][3]={{0,0,0},{1,0,0},{1,1,0},{0,1,0},{0,0,1},{1,0,1},{1,1,1},{0,1,1}};
MeshDB<double> model(std::size_t count) {
    ModelBuilder<double> b; b.add_material({1,1,{2.,100.,.25},0});b.add_part(1);
    for(std::size_t e=0;e<count;++e) {
        Id base=8*e+1;
        if(e%2) {
            for(unsigned a=0;a<8;++a) b.add_node(base+a,{3.*double(e)+corner[a][0],double(corner[a][1]),double(corner[a][2])});
            b.add_element(e+1,Topology::Hex8,{base,base+1,base+2,base+3,base+4,base+5,base+6,base+7},1,1,8);
        } else {
            b.add_node(base,{3.*double(e),0,0});b.add_node(base+1,{3.*double(e)+1,0,0});
            b.add_node(base+2,{3.*double(e),1,0});b.add_node(base+3,{3.*double(e),0,1});
            b.add_element(e+1,Topology::Tet4,{base,base+1,base+2,base+3},1,1);
        }
    }
    return b.finalize();
}
template<class F> double seconds(F fn,unsigned repeats) {
    auto begin=std::chrono::steady_clock::now();for(unsigned r=0;r<repeats;++r)fn();
    return std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count()/repeats;
}
double median(std::vector<double> values) {std::sort(values.begin(),values.end());return values[values.size()/2];}
int main(int argc,char **argv) {
    try {
        std::size_t count=argc>1?std::stoull(argv[1]):2000;
        unsigned repeats=argc>2?static_cast<unsigned>(std::stoul(argv[2])):10;
        unsigned samples=argc>3?static_cast<unsigned>(std::stoul(argv[3])):15;
        if(count<2||!repeats||samples<3||samples%2==0)throw std::invalid_argument("elements>=2 repeats>0 odd samples>=3");
        auto mesh=model(count); StateDB<double> state(mesh); CpuExplicit<double> cpu(mesh,state);
        auto u=state.data().displacement.write();
        for(std::size_t n=0;n<u.count;++n)for(unsigned c=0;c<3;++c)u(n,c)=.001*std::sin(double(n+c+1));
        ReferenceConsumer<double> consumer(cpu.plan(),state); auto node=NodeStateView<double>::capture(state);
        ForceSnapshot<double> out(mesh.nodes(),state.data().stress.size());
        ResultSink<double> sink(out.internal_force,out.stress,out.strain);
        const auto &p=cpu.plan();const auto &h=*p.hex;
        BorrowedField<Index,4> tn(p.nodes);BorrowedField<double,12> tg(p.gradients);BorrowedField<double> tw(p.volume),tl(p.lambda),tm(p.mu);
        BorrowedField<Index,8> hn(h.nodes);BorrowedField<double,24> hg(h.gradients);BorrowedField<double> hw(h.weights),hl(h.lambda),hm(h.mu);
        Field<std::size_t> ips(h.ip_offsets.size());for(std::size_t i=0;i<ips.size();++i)ips.write()(i)=h.ip_offsets[i];
        BorrowedField<std::size_t> hip(ips);
        Field<double,12> local_t(p.nodes.size());Field<double,24> local_h(h.nodes.size());
        // Identical kernel loops, arrays and outputs. Only acquisition changes: direct raw vs checked contract borrows.
        auto kernels=[&](bool checked) {
            TetViews<double> t=checked?TetViews<double>{tn.resolve(),tg.resolve(),tw.resolve(),tl.resolve(),tm.resolve()}:p.views();
            HexViews<double> hv=checked?HexViews<double>{hn.resolve(),hg.resolve(),hw.resolve(),hl.resolve(),hm.resolve(),hip.resolve()}:
                HexViews<double>{h.nodes.read(),h.gradients.read(),h.weights.read(),h.lambda.read(),h.mu.read(),ips.read()};
            auto displacement=checked?node.displacement.resolve():state.data().displacement.read();
            auto lt=local_t.write();auto lh=local_h.write();auto stress=out.stress.write();auto strain=out.strain.write();
            for(std::size_t e=0;e<p.nodes.size();++e)if(p.is_tet[e])tet_force(e,t,displacement,lt,stress,strain,p.ip_offsets[e]);
            for(std::size_t e=0;e<h.nodes.size();++e)hex_force(e,hv,displacement,lh,stress,strain,true);
        };
        cpu.forces(state);consumer.evaluate(node,sink);kernels(false);kernels(true);
        std::vector<double> raw,view,baseline,reference,ratios,end_ratios;
        for(unsigned sample=0;sample<samples;++sample) {
            double a,b,c,d;
            if(sample%2) {b=seconds([&]{kernels(true);},repeats);a=seconds([&]{kernels(false);},repeats);
                d=seconds([&]{consumer.evaluate(node,sink);},repeats);c=seconds([&]{cpu.forces(state);},repeats);}
            else {a=seconds([&]{kernels(false);},repeats);b=seconds([&]{kernels(true);},repeats);
                c=seconds([&]{cpu.forces(state);},repeats);d=seconds([&]{consumer.evaluate(node,sink);},repeats);}
            raw.push_back(a);view.push_back(b);baseline.push_back(c);reference.push_back(d);ratios.push_back(100*(b/a-1));end_ratios.push_back(100*(d/c-1));
        }
        double checksum=0;for(std::size_t i=0;i<out.stress.size();++i)checksum+=out.stress.read()(i,0);
        auto bounds=[](const std::vector<double>& x){return std::minmax_element(x.begin(),x.end());};
        auto [rmin,rmax]=bounds(ratios);auto[emin,emax]=bounds(end_ratios);
        std::cout<<std::setprecision(9)<<"DBF5 consumer benchmark; alternating paired samples; serial FP64\n"
            <<"elements="<<count<<" nodes="<<mesh.nodes()<<" samples="<<samples<<" repeats="<<repeats<<'\n'
            <<"identical_force_kernel_raw_median_us="<<1e6*median(raw)<<" checked_median_us="<<1e6*median(view)<<'\n'
            <<"paired_checked_overhead_percent median="<<median(ratios)<<" min="<<*rmin<<" max="<<*rmax<<'\n'
            <<"full_cpu_median_us="<<1e6*median(baseline)<<" reference_median_us="<<1e6*median(reference)<<'\n'
            <<"full_consumer_difference_percent median="<<median(end_ratios)<<" min="<<*emin<<" max="<<*emax<<'\n'
            <<"checksum="<<checksum<<"; full-consumer difference includes assembly traversal; no universal 1.3% guarantee\n";
    }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
}

