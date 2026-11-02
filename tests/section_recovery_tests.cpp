#include <sdb/physics/section_run.hpp>
#include <iostream>
using namespace sdb;
using namespace sdb::physics;
using namespace sdb::lifecycle;
int main(int argc,char**argv) {
    try {
        if(argc!=3) throw std::invalid_argument("fixtures scratch");
        std::filesystem::create_directories(argv[2]);
        for(bool shell:{false,true}) {
            auto name=shell?"section_shell.inp":"section_beam.inp";
            auto specimen=std::filesystem::path(argv[1])/name;
            std::ifstream deck(specimen);
            std::string text(std::istreambuf_iterator<char>(deck),{});
            text.insert(text.find("*End Step"),"*Output,field,frequency=50\n*Node Output\nU,UR\n*Element Output\nS,E\n");
            auto ir=AbaqusInpReader{}.buffer(text,specimen).model;
            auto m=ModelCompiler{}.compile<double>(ir);
            SectionElement element(m,0,m.sections[0]);
            StateDB<double> state(m.mesh,true);
            // Positive curvature about the local y-axis: axial strain = z*kappa.
            for(Index n=0;n<m.mesh.nodes();++n)
                state.data().rotation->angle.write()(n,1)=.02*m.mesh.reference()(n,0);
            auto e=element.recover(state,false),s=element.recover(state,true);
            const unsigned width=shell?3:1;
            auto bottom=e[0], top=e[e.size()-width];
            if(std::abs(bottom+.001)>1e-12 || std::abs(top-.001)>1e-12 ||
               std::abs(s[0]+.1)>1e-12 || std::abs(s[s.size()-width]-.1)>1e-12)
                throw std::runtime_error("opposed fibre bending values");
            SectionRun<double> run(m);
            auto file=std::filesystem::path(argv[2])/(std::string(name)+".sdbres");
            std::filesystem::remove(file);
            { NativeResultStorage out(file,run.result_model(),false,false,native::default_limit,{},false,ResultFormat::TypedIndexed3);
              ResultWriter w(out);run.initialize();w.write(run.snapshot());
              while(run.advance()) w.write(run.snapshot());
            }
            ResultReader reader(file);
            auto f=reader.typed_field(reader.index.size()-1,"S");
            if(reader.index.size()!=21 || f.typed->addresses.size()!=element.sample_count() ||
               f.typed->addresses.back().section_point!=element.sample_count() ||
               !f.typed->basis_id || !(typed_values<double>(f)[0]>0))
                throw std::runtime_error("section schedule/basis/values");
        }
        std::cout<<"B31/S4R elastic bending and scheduled section result round trip PASS\n";
    } catch(const std::exception&e){std::cerr<<e.what()<<std::endl;return 1;}
}
