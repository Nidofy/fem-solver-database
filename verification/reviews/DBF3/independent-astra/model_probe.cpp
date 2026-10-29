#define main upstream_model_main
#include "../../../../build-dbf3-review/fixed47/tests/dbf3_model_tests.cpp"
#undef main
int main(){try{
 std::filesystem::path dir="verification/reviews/DBF3/independent-astra/model";std::filesystem::create_directories(dir);
 contracts<float>(dir);contracts<double>(dir);parser("tests/fixtures/dbf3");
 auto ir=fixture();ir.parts[0].name="P";ir.instances={{"A","P",{},{},{}},{"B","P",{10,0,0},{0,0,0,0,0,1,90},{}}};
 for(auto&s:ir.sets)s.scope="P";ir.surfaces.clear();ir.foundation.solid_sections[0].assignment.scope="P";
 ir.extensions.couplings.clear();ir.foundation.nodal_loads.clear();ir.foundation.initial_conditions.clear();ir.foundation.activations.clear();ir.extensions.initial_events.clear();ir.extensions.activation_policies.clear();
 for(auto&p:ir.extensions.property_distributions)p.scope="P";
 IRCoupling c;c.id=ConstraintId(10);c.name="BONLY";c.reference={"P","B",6};c.nodes="B.DEPENDENT";c.dof_mask=7;ir.extensions.couplings={c};
 auto m=ModelCompiler{}.compile<double>(ir);
 for(auto instance:{"A","B"}){auto e=m.sources.element_id({"P",instance,1});check(m.foundation.property(e,"yield").constant.real[0]==15,"scoped property repeats across instances");}
 auto coupling=m.foundation.coupling(ConstraintId(10));for(auto n:coupling.nodes)check(m.sources.nodes.at(n.value()).key.instance=="B","coupling confined to B");
 auto changed=ir;changed.extensions.property_distributions[0].region="A.LEFT";auto single=ModelCompiler{}.compile<double>(changed);
 check(single.foundation.property(single.sources.element_id({"P","B",1}),"yield").constant.real[0]==10,"qualified scope changes only A");
 auto legacy=ReaderRegistry{}.read("tests/fixtures/abaqus/forced_tet.inp").model;
 legacy.amplitudes={{"STEP",false,{{0,0},{.2,2}},{}}};legacy.steps[0].loads[0].actions[0].amplitude="STEP";
 auto step2=legacy.steps[0];step2.name="SECOND";step2.loads.clear();legacy.steps.push_back(step2);
 auto lm=ModelCompiler{}.compile<double>(legacy);StateDB<double>s(lm.mesh);
 LoadEvaluator::apply(lm.analysis,0,.2,.2,s);auto n=lm.sources.node_id({"","",40}).value();auto end=s.data().external_force.read()(n,0);
 LoadEvaluator::apply(lm.analysis,1,0,.2,s);auto propagated=s.data().external_force.read()(n,0);
 std::cout<<"PROBE inherited executable step-time amplitude end="<<end<<" propagated next step start="<<propagated<<" expected=2 executable="<<lm.report.executable_cpu<<"\n";
 std::cout<<"Independent model checks="<<checks<<" PASS (legacy propagation separately reported)\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
