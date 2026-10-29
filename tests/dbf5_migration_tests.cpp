#include <sdb/migration/preprocessed.hpp>
#include <sdb/checkpoint.hpp>
#include <sdb/dynamics.hpp>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
using namespace sdb;
using namespace sdb::migration;
unsigned checks = 0;
void check(bool yes, const char *message) {
    ++checks; if (!yes) throw std::runtime_error(message);
}
template<class F> void rejects(F &&f, const std::string &message) {
    ++checks;
    try { f(); }
    catch (const std::exception &e) {
        if (std::string(e.what()).find(message) != std::string::npos) return;
        throw std::runtime_error(std::string("wrong diagnostic: ") + e.what() + "; expected " + message);
    }
    throw std::runtime_error("expected rejection: " + message);
}
PreprocessedSnapshot fixture() {
    PreprocessedSnapshot s;
    s.fnl.ndlb = {10, 40, 20, 90, 70, 100, 110, 120, 130, 140, 150, 160, 170};
    s.fnl.reference = {{0,0,0}, {1,0,0}, {0,1,0}, {0,0,1}, {1,1,1},
        {3,0,0}, {4,0,0}, {4,1,0}, {3,1,0}, {3,0,1}, {4,0,1}, {4,1,1}, {3,1,1}};
    s.fnl.equations = 38;
    s.fnl.idarray.resize(13);
    int eq = 38;
    for (std::size_t i = 0; i < 13; ++i) {
        s.fnl.seqnd.push_back({s.fnl.ndlb[i], static_cast<std::int64_t>(i + 1)});
        for (unsigned c = 0; c < 3; ++c) s.fnl.idarray[i][c] = i == 0 && c == 0 ? 0 : eq--;
    }
    // Input capture order differs from label and sequence order deliberately.
    std::reverse(s.fnl.seqnd.begin(), s.fnl.seqnd.end());
    s.materials = {{7, 1, {2,1000,.25}, 0, {}}};
    s.properties = {{99,77,55}};
    auto add = [&](Id id, Topology t, std::vector<std::int64_t> nodes) {
        FegElement e; e.egroup_elab=id; e.egroup_mset=7; e.egroup_gset=99;
        e.topology=t; e.integration_points=t==Topology::Tet4?1:8; e.egroup_elseqnd=nodes;
        for (auto seq : nodes) {
            e.egroup_elndlb.push_back(s.fnl.ndlb[static_cast<std::size_t>(seq - 1)]);
            for (auto v : s.fnl.idarray[static_cast<std::size_t>(seq - 1)]) e.egroup_eldof.push_back(v);
        }
        for (unsigned c=0;c<3;++c) for(auto seq:nodes)
            e.egroup_xyz.push_back(static_cast<float>(s.fnl.reference[static_cast<std::size_t>(seq - 1)][c]));
        s.elements.push_back(std::move(e));
    };
    add(900,Topology::Tet4,{1,2,3,4}); add(100,Topology::Tet4,{2,3,4,5});
    add(500,Topology::Hex8,{6,7,8,9,10,11,12,13});
    return s;
}
std::string report(const MigratedModel &m, const StateDB<double> &state,
                   std::span<const std::array<double,3>> reduced) {
    std::ostringstream out; out<<"DBF5 FNL-FEG synthetic transfer golden v1\n";
    out<<"nodes "<<m.mesh.nodes()<<" elements "<<m.mesh.elements()<<" equations "<<m.fnl.equations
       <<" storage_only "<<m.mesh.requires_storage_only()<<"\n";
    for (Index n=0;n<m.mesh.nodes();++n) {
        out<<"node "<<m.mesh.node_id(n)<<" seq "<<n+1<<" eq";
        for(auto eq:m.fnl.idarray[n]) out<<' '<<eq;
        out<<" x";for(unsigned c=0;c<3;++c)out<<' '<<m.mesh.reference()(n,c);
        out<<" u";for(unsigned c=0;c<3;++c)out<<' '<<state.data().displacement.read()(n,c);
        out<<" f";for(unsigned c=0;c<3;++c)out<<' '<<reduced[n][c];out<<'\n';
    }
    for(Index e=0;e<m.mesh.elements();++e) {
        const auto &element=m.mesh.element(e);
        out<<"element "<<element.id<<" nodes";
        for(Index n:m.mesh.connectivity(e))out<<' '<<m.mesh.node_id(n);
        out<<" material "<<m.mesh.materials()[element.material].id
           <<" part "<<m.mesh.parts()[element.part].id<<" property "<<m.mesh.parts()[element.part].property
           <<" points "<<element.integration_points<<'\n';
    }
    return out.str();
}
std::string read(const std::filesystem::path &p) {
    std::ifstream in(p); if(!in)throw std::runtime_error("missing golden fixture");
    return {std::istreambuf_iterator<char>(in),{}};
}
int main(int argc,char **argv) {
    try {
        if(argc<3)throw std::runtime_error("usage: migration_tests golden.txt scratch [--benchmark]");
        const std::filesystem::path scratch=argv[2];std::filesystem::create_directories(scratch);
        auto s=fixture();auto model=import_preprocessed(s);
        check(model.mesh.nodes()==13&&model.mesh.elements()==3,"mixed fixture shape");
        check(model.mesh.node_index(40)==1&&model.mesh.node_index(20)==2,"sparse unordered labels preserve sequence");
        check(model.mesh.parts()[0].id==77&&model.mesh.parts()[0].property==55,"explicit property mapping");
        check(model.mesh.constraints().empty(),"inactive equation does not invent SPC");
        StateDB<double> state(model.mesh);
        rejects([&]{TetPlan<double> plan(model.mesh,state);},"storage-only");
        std::vector<double> equations(38);for(std::size_t i=0;i<equations.size();++i)equations[i]=double(i+1);
        std::vector<std::array<double,3>> inactive(13);inactive[0][0]=.5;
        const auto nodal=gather_equations(s.fnl,equations,inactive);
        check(nodal[0]==std::array<double,3>{.5,38,37}&&nodal[12]==std::array<double,3>{3,2,1},"independent reordered equation gather oracle");
        NodalKinematics k{nodal,std::vector<std::array<double,3>>(13),std::vector<std::array<double,3>>(13)};
        initialize_kinematics(model,state,k);
        auto view=consumer::NodeStateView<double>::capture(state);
        auto work=make_private_scratch(model,ElementId(0),view);
        check(work.el_dof==s.elements[0].egroup_eldof&&work.displacement[0][0]==.5,"view to private legacy scratch");
        for(auto &eq:work.el_dof)--eq;
        work.reference[0][0]=999;work.displacement[0][0]=999;
        check(model.elements[0].one_based_equations==s.elements[0].egroup_eldof,"adversarial one-based decrement isolated");
        check(model.mesh.reference()(0,0)==0&&state.data().displacement.read()(0,0)==.5,"adversarial mutable work is private");
        std::vector<NodalContribution> terms={{ElementId(0),1,0,2},{ElementId(1),0,0,3},
                                            {ElementId(2),0,2,7},{ElementId(0),0,1,4}};
        auto reduced=reduce_nodal(model.mesh,terms);
        check(reduced[1][0]==5&&reduced[0][1]==4&&reduced[5][2]==7,"independent nodal reduction oracle");
        std::mt19937 random(42);
        for(unsigned repeat=0;repeat<40;++repeat) {
            std::shuffle(terms.begin(),terms.end(),random);
            check(reduce_nodal(model.mesh,terms)==reduced,"reducer worker order independent");
        }
        // Isolated cancellation oracle: three synthetic contributions share a
        // node. Label order 100(+1e16),300(-1e16),900(+1) must produce exactly 1.
        auto cancellation=s;cancellation.elements.push_back(s.elements[0]);
        cancellation.elements.back().egroup_elab=300;
        auto cancel_model=import_preprocessed(cancellation);
        std::vector<NodalContribution> cancel={{ElementId(0),1,0,1},{ElementId(1),0,0,1e16},
                                               {ElementId(3),1,0,-1e16}};
        for(unsigned repeat=0;repeat<20;++repeat) {
            std::shuffle(cancel.begin(),cancel.end(),random);
            check(reduce_nodal(cancel_model.mesh,cancel)[1][0]==1,"nonassociative reduction label-order oracle");
        }
        auto precision=s;precision.fnl.reference[4][0]=1.00000001;
        auto precise_model=import_preprocessed(precision);StateDB<double> precise_state(precise_model.mesh);
        auto precise_work=make_private_scratch(precise_model,ElementId(1),consumer::NodeStateView<double>::capture(precise_state));
        check(precise_work.reference[3][0]==1.00000001&&precise_work.legacy_reference[3][0]==1.0f,
              "canonical double and legacy FEG float precision remain distinct");
        std::vector<std::pair<Id,std::array<double,3>>> written;
        backwrite_nodal(model.mesh,reduced,[&](Id label,auto value){written.push_back({label,value});});
        check(written.size()==13&&written[1].first==40&&written[1].second[0]==5,"backwrite original labels and values");
        const auto text=report(model,state,reduced);
        std::ofstream(scratch/"migration-actual.txt")<<text;
        check(text==read(argv[1]),"checked golden acceptance report");
        save_checkpoint(scratch/"migration.sdbrst",model.mesh,state);
        StateDB<double> restored(model.mesh);load_checkpoint(scratch/"migration.sdbrst",model.mesh,restored);
        check(report(model,restored,reduced)==text,"existing checkpoint state roundtrip");
        auto bad=[&](auto mutate,const std::string &why){auto copy=s;mutate(copy);rejects([&]{import_preprocessed(copy);},why);};
        bad([](auto &v){v.fnl.ndlb[1]=10;},"duplicate node");
        bad([](auto &v){v.fnl.ndlb[0]=0;},"zero/duplicate");
        bad([](auto &v){v.fnl.ndlb[0]=static_cast<Id>(-1);},"out-of-range node label");
        bad([](auto &v){v.fnl.seqnd[0].second=0;},"one-based inverse");
        bad([](auto &v){v.fnl.seqnd[0].second=999;},"one-based inverse");
        bad([](auto &v){v.fnl.seqnd[0]=v.fnl.seqnd[1];},"duplicate/unknown");
        bad([](auto &v){v.fnl.reference.pop_back();},"shape mismatch");
        bad([](auto &v){v.fnl.idarray[0][0]=-1;},"negative/out-of-range");
        bad([](auto &v){v.fnl.idarray[0][0]=39;},"negative/out-of-range");
        bad([](auto &v){v.fnl.equations=std::numeric_limits<std::uint32_t>::max();},"signed legacy ID range");
        bad([](auto &v){v.fnl.reference[0][0]=std::numeric_limits<double>::quiet_NaN();},"nonfinite coordinate");
        bad([](auto &v){v.elements[0].egroup_elab=100;},"duplicate element");
        bad([](auto &v){v.elements[0].egroup_elseqnd[0]=0;},"one-based and in range");
        bad([](auto &v){v.elements[0].egroup_elseqnd[0]=-1;},"one-based and in range");
        bad([](auto &v){v.elements[0].egroup_elndlb[0]=20;},"label/sequence mismatch");
        bad([](auto &v){v.elements[0].egroup_eldof[0]=1;},"LM/IDARRAY mismatch");
        bad([](auto &v){v.elements[0].egroup_eldof.pop_back();},"shape mismatch");
        bad([](auto &v){v.elements[0].egroup_xyz.pop_back();},"shape mismatch");
        bad([](auto &v){v.elements[0].egroup_xyz[4]=999;},"packed coordinate");
        bad([](auto &v){v.elements[0].egroup_xyz[0]=std::numeric_limits<float>::infinity();},"packed coordinate");
        bad([](auto &v){v.elements[0].topology=Topology::Quad4;},"unsupported FEG family");
        bad([](auto &v){v.elements[0].egroup_mCsys=1;},"unsupported local");
        bad([](auto &v){v.elements[0].egroup_lcns_exists=true;},"unsupported local");
        bad([](auto &v){v.elements[0].hybrid_or_internal_nodes=true;},"unsupported local");
        bad([](auto &v){v.elements[0].egroup_mset=8;},"missing translated material");
        bad([](auto &v){v.properties.clear();},"missing explicit property");
        bad([](auto &v){v.unsupported_fields={"thermal"};},"unsupported populated");
        rejects([&]{gather_equations(s.fnl,std::span<const double>{},inactive);},"gather shape");
        inactive[0][0]=std::numeric_limits<double>::quiet_NaN();
        rejects([&]{gather_equations(s.fnl,equations,inactive);},"inactive DOF");
        auto bad_k=k;bad_k.acceleration.pop_back();
        rejects([&]{initialize_kinematics(model,state,bad_k);},"kinematics shape");
        check(state.data().displacement.read()(0,0)==.5,"invalid initialization leaves state unchanged");
        rejects([&]{make_private_scratch(model,ElementId(),view);},"invalid element");
        auto stale=consumer::NodeStateView<double>::capture(restored);
        restored.data().displacement.resize(13);
        rejects([&]{make_private_scratch(model,ElementId(0),stale);},"expired field view");
        auto invalid=terms;invalid.push_back(invalid[0]);
        rejects([&]{reduce_nodal(model.mesh,invalid);},"duplicate contribution");
        invalid=terms;invalid[0].local_node=99;rejects([&]{reduce_nodal(model.mesh,invalid);},"local node/component");
        invalid=terms;invalid[0].element=ElementId();rejects([&]{reduce_nodal(model.mesh,invalid);},"invalid element");
        invalid=terms;invalid[0].value=std::numeric_limits<double>::quiet_NaN();
        rejects([&]{reduce_nodal(model.mesh,invalid);},"nonfinite value");
        auto bad_values=reduced;bad_values.back()[0]=std::numeric_limits<double>::infinity();unsigned calls=0;
        rejects([&]{backwrite_nodal(model.mesh,bad_values,[&](Id,auto){++calls;});},"nonfinite value");
        check(calls==0,"backwrite validates all values before sink mutation");
        std::cout<<"DBF5 migration: "<<checks<<" checks passed\n";
        if(argc>3&&std::string(argv[3])=="--benchmark") {
            constexpr int iterations=2000;std::uint64_t checksum=0;
            auto begin=std::chrono::steady_clock::now();
            for(int i=0;i<iterations;++i){auto copy=import_preprocessed(s);checksum+=copy.mesh.nodes();}
            auto captured=std::chrono::steady_clock::now();
            for(int i=0;i<iterations;++i){auto copy=make_private_scratch(model,ElementId(0),view);checksum+=copy.el_dof.size();}
            auto copied=std::chrono::steady_clock::now();
            for(int i=0;i<iterations;++i){auto values=reduce_nodal(model.mesh,terms);checksum+=static_cast<std::uint64_t>(values[1][0]);}
            auto ended=std::chrono::steady_clock::now();
            auto us=[&](auto a,auto b){return std::chrono::duration<double,std::micro>(b-a).count()/iterations;};
            std::cout<<"local baseline iterations="<<iterations<<" snapshot_to_frozen_us="<<us(begin,captured)
                <<" private_tet_scratch_us="<<us(captured,copied)<<" reduction_us="<<us(copied,ended)<<" checksum="<<checksum<<'\n';
        }
    }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}
