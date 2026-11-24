// Kept source-compatible with released SDKs so an independent build can emit the golden.
#include <sdb/io/native.hpp>
#include <sdb/model_ir/model_ir.hpp>
#include <iostream>
using namespace sdb::lifecycle;
static ModelIR example(unsigned version) {
    ModelIR m; m.schema=version; m.units="SI"; m.assembly="fixture"; m.mode=ImportMode::MeshOnly;
    IRPart part; part.name="p"; part.nodes.push_back({17,{1.25,-0.0,1.5},{"fixture",4,2}});
    m.parts.push_back(part); IRAmplitude amp; amp.name="curve"; amp.points={{0,0},{1,4}}; m.amplitudes.push_back(amp);
    if(version>=3) { IRCoordinate c; c.id=sdb::CoordinateId(1); c.name="local"; m.foundation.coordinates.push_back(c); }
    if(version>=4) { IRAmplitudePolicy p; p.amplitude="curve"; m.extensions.amplitude_policies.push_back(p); }
    if(version>=5) { IRPlastic p; p.name="p"; p.points={{0.25,1}}; m.plastic.push_back(p); }
    if(version>=6) m.penalty_stiffness=2.25;
    if(version>=8) m.finite_geometry=true;
    return m;
}
int main(int argc,char **argv) { try {
    if(argc!=3) return 2;
    std::string op=argv[1];
    if(op=="write") {
        native::atomic_stream(argv[2],[&](auto &out) {
            for(unsigned v=1;v<=10;++v) native::record(out,v,native::pack(example(v)));
        });
    } else if(op=="read") {
        std::ifstream in(argv[2],std::ios::binary);
        for(unsigned v=1;v<=10;++v) {
            auto b=native::record(in,v); auto m=native::unpack<ModelIR>(b);
            if(m.schema!=v || native::pack(m)!=b || native::pack(example(v))!=b) return 1;
        }
        native::end(in);
    } else return 2;
    return 0;
} catch(const std::exception &e) { std::cerr<<e.what()<<'\n'; return 1; } }
