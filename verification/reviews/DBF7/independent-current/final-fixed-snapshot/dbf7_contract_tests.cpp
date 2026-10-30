#include <sdb/foundation.hpp>
#include <sdb/input/thermal_contract.hpp>
#include <iostream>
#include <thread>
#include <atomic>
using namespace sdb;
namespace n=sdb::lifecycle::native;
static int checks=0;
void check(bool v,const char *s){++checks;if(!v)throw std::runtime_error(s);}
template<class F> void rejects(F f){++checks;try{f();}catch(const std::exception&){return;}throw std::runtime_error("expected rejection");}
DofSpace space() {
    DofSpaceData d; d.model=42;d.nodes=2;d.elements=1;
    d.variables={{1,1,DofQuantity::Temperature,"T","K","scalar"}};
    d.bindings={{{DofOwner::Node,0,0,1,0},DofStatus::Free,0,0,{}},
                {{DofOwner::Node,1,0,1,0},DofStatus::Prescribed,no_equation,300,{}}};
    d.external_equations={500};d.solver_to_canonical={0};return DofSpace(d);
}
consumer::v1::Batch batch(const DofSpace &d) {
    consumer::v1::BatchData b;b.model=42;b.equation_space=d.identity();b.formulation="thermal-boundary";
    b.topology.name="LINE2";b.topology.nodes=2;b.elements={0};b.connectivity={0,1};
    b.sample_offsets={0,0};b.dof_offsets={0,2};b.dof_slots={0,1};
    return {b,d,consumer::v1::MaterialCatalog({}),consumer::v1::SectionCatalog({})};
}
FieldDescriptor desc(){FieldDescriptor d;d.name="T";d.association=Association::Node;d.location=FieldLocation::Entity;d.units="K";return d;}
StateLayout layout(){StateLayout l;l.domain=2;l.addresses.resize(2);l.addresses[0].entity=1;l.addresses[1].entity=2;return l;}
RegisteredState state(const DofSpace &d,std::uint64_t consumer=0){RegisteredState s;s.bind_context(42,d.identity(),consumer);
    s.add<Field<double>>(desc(),2,"temperature",1,{300},0,layout());s.freeze();return s;}
int main(int argc,char **argv) {try {
    if(argc==3 && std::string(argv[1])=="read") {
        std::filesystem::path dir=argv[2];auto d=space();auto b=batch(d);auto s=state(d,b.identity());
        auto cursor=FoundationBundle::restore(dir/"bundle.sdb",b,s);
        check(cursor.increment==5 && s.get<Field<double>>("T").read()(0)==350,"fresh process bundle");
        auto sp=SpectralSeries::load(dir/"spectral.sdb");check(sp.imaginary.back()==5,"fresh process spectral");
        std::cout<<"DBF7 reader checks="<<checks<<" PASS\n";return 0;
    }
    if(argc!=2)throw std::invalid_argument("scratch path");
    std::filesystem::path dir=argv[1];std::filesystem::create_directories(dir);
    auto d=space();auto b=batch(d);auto s=state(d,b.identity());s.get<Field<double>>("T").write()(0)=350;
    BundleCursor cursor;cursor.time=0.25;cursor.increment=5;cursor.consumer_schema="thermal-test/v1";
    auto file=dir/"bundle.sdb";FoundationBundle::save(file,b,s,cursor);
    auto info=FoundationBundle::inspect(file);check(info.fields.size()==1 && info.fields[0].rows==2,"catalog rows");
    check(info.fields[0].initial_scalars.size()==8 && info.batch.identity()==b.identity(),"inspect typed initializer");
    auto out=state(d,b.identity());auto restored=FoundationBundle::restore(file,b,out);
    check(restored.increment==5 && out.get<Field<double>>("T").read()(0)==350,"bundle restore");
    rejects([&]{FoundationBundle::save(file,b,s,cursor,32);});
    check(FoundationBundle::restore(file,b,out).increment==5,"budget failure preserves published bundle");
    auto corrupt=dir/"bad-bundle.sdb";std::filesystem::copy_file(file,corrupt,std::filesystem::copy_options::overwrite_existing);
    std::filesystem::resize_file(corrupt,std::filesystem::file_size(corrupt)-1);
    auto before=out.find("T").block(0,2);rejects([&]{FoundationBundle::restore(corrupt,b,out);});
    check(out.find("T").block(0,2)==before,"failed bundle leaves target unchanged");
    auto neutral=state(d);auto bytes=state_payload_bytes(neutral);StateSession owner(neutral,bytes*3);
    {auto r=owner.read();auto r2=owner.read();check(r.field<Field<double>>("T").at(0)==300,"shared readers");
     rejects([&]{owner.write();});rejects([&]{owner.begin_trial();});rejects([&]{owner.remap("T",layout());});
     std::atomic<int> ok=0;std::thread t([&]{auto rr=owner.read();if(rr.field<Field<double>>("T").at(0)==300)++ok;});t.join();check(ok==1,"cross-thread read lease");}
    owner.begin_trial();{auto w=owner.write();w.field<Field<double>>("T").at(0)=333;rejects([&]{owner.read();});}
    owner.rollback();{auto r=owner.read();check(r.field<Field<double>>("T").at(0)==300,"session rollback");}
    auto expired=[&]{auto w=owner.write();return w.field<Field<double>>("T");}();
    {auto r=owner.read();rejects([&]{expired.at(0)=500;});check(r.field<Field<double>>("T").at(0)==300,"expired write lease rejected");}
    auto l=layout();std::swap(l.addresses[0],l.addresses[1]);owner.remap("T",l);
    check(owner.memory().high_water_payload==bytes*2,"accounted staging high water");
    StateSession small(neutral,bytes);rejects([&]{small.begin_trial();});check(small.memory().trial_payload==0,"budget failure no trial");
    auto standalone=dir/"state.sdb";owner.save(standalone,d);owner.restore(standalone,d);
    Unit kelvin;kelvin.symbol="K";kelvin.powers[4]=1;Unit celsius=kelvin;celsius.symbol="degC";celsius.offset=273.15;
    check(std::abs(celsius.convert(20,kelvin)-293.15)<1e-12,"affine temperature conversion");
    Unit metre;metre.symbol="m";metre.powers[0]=1;Unit millimetre=metre;millimetre.scale=.001;
    check(millimetre.convert(1000,metre)==1,"unit conversion");rejects([&]{kelvin.convert(1,metre);});rejects([&]{celsius.combine(metre);});
    check(metre.combine(metre).powers[0]==2,"dimension product");
    EquationMatrix<> matrix;matrix.row_space=d.identity();matrix.column_space=d.identity();matrix.rows=1;matrix.columns=1;
    matrix.offsets={0,1};matrix.indices={0};matrix.values={7};matrix.symmetric=true;matrix.require(d,d);
    check(EquationMatrix<>::decode(matrix.encode()).values[0]==7,"matrix roundtrip");
    auto different=d.data();different.external_equations[0]=777;rejects([&]{matrix.require(DofSpace(different),d);});
    auto wide=matrix;wide.symmetric=false;wide.columns=UINT64_C(4294967298);wide.indices={UINT64_C(4294967297)};
    check(EquationMatrix<>::decode(wide.encode()).indices[0]>UINT32_MAX,"64-bit column preserved without huge allocation");
    auto bad=matrix;bad.offsets={0,99};rejects([&]{bad.validate();});
    auto cross=matrix;cross.rows=cross.columns=2;cross.offsets={0,1,99};rejects([&]{cross.validate();});
    AnalysisContract a;a.equations=d.identity();a.consumer_schema="thermal-step/v1";a.output_times={0,.5,1};
    SlotAssignment x;x.slot=1;x.role=AssignmentRole::Prescribed;x.unit=kelvin;x.times={0,1};x.values={300,400};a.assignments={x};
    auto a2=AnalysisContract::decode(a.encode(d),d);check(a2.assignments[0].evaluate(.5)==350,"thermal assignment");
    a.assignments[0].slot=0;rejects([&]{a.validate(d);});
    Unit watt;watt.symbol="W";watt.powers={2,1,-3,0,0,0,0};
    std::istringstream fragment("*INITIAL CONDITIONS, TYPE=TEMPERATURE\n10,2.9D2\n*TEMPERATURE\n20,333\n*CFLUX\n10,11,5\n");
    auto thermal=lifecycle::read_thermal_assignments(fragment,d,{{10,0},{20,1}},kelvin,watt,0,1);
    check(thermal.assignments.size()==3 && thermal.assignments[0].values[0]==290,"thermal input storage subset");
    std::istringstream unsupported("*CFLUX, AMPLITUDE=A\n10,11,5\n");
    rejects([&]{lifecycle::read_thermal_assignments(unsupported,d,{{10,0}},kelvin,watt,0,1);});
    std::istringstream too_long(std::string(100,'x'));
    rejects([&]{lifecycle::read_thermal_assignments(too_long,d,{{10,0}},kelvin,watt,0,1,10);});
    SpectralSeries sp;sp.model=42;sp.equations=d.identity();sp.field=desc();sp.unit=kelvin;sp.layout=layout();
    sp.coordinates={0,10,20};sp.real={1,2,3,4,5,6};sp.imaginary={0,1,2,3,4,5};sp.save(dir/"spectral.sdb");
    auto sp2=SpectralSeries::load(dir/"spectral.sdb");check(sp2.imaginary==sp.imaginary,"complex storage exact");
    auto wrong_unit=sp;wrong_unit.field.units="m";rejects([&]{wrong_unit.validate();});
    check(sp2.interval(10,20)==std::pair<std::size_t,std::size_t>{1,3},"frequency closed interval");
    sp.axis=SpectralAxis::ModeNumber;rejects([&]{sp.validate();});sp.normalization=ModeNormalization::UnitMass;sp.coordinates={1,2,3};sp.validate();
    sp.coordinates[1]=1.5;rejects([&]{sp.validate();});
    std::cout<<"DBF7 checks="<<checks<<" PASS\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
