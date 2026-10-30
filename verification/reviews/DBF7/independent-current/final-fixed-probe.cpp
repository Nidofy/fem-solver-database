#define main dbf7_fixture_main
#include "final-fixed-snapshot/dbf7_contract_tests.cpp"
#undef main
int main(){try{
  auto d=space();Unit k;k.symbol="K";k.powers[4]=1;Unit w;w.symbol="W";w.powers={2,1,-3,0,0,0,0};
  std::istringstream broken("*CFLUX\n10,11,2\n");broken.setstate(std::ios::failbit);
  rejects([&]{sdb::lifecycle::read_thermal_assignments(broken,d,{{10,0}},k,w,0,1);});
  std::istringstream empty("** comment\n"); rejects([&]{sdb::lifecycle::read_thermal_assignments(empty,d,{{10,0}},k,w,0,1);}); std::istringstream good("*CFLUX\n10,11,2\n"); check(sdb::lifecycle::read_thermal_assignments(good,d,{{10,0}},k,w,0,1).assignments.size()==1,"valid thermal input");
  RegisteredState s;auto fd=desc();fd.scalar=ScalarType::UInt64;fd.shape={2};fd.components={"A","B"};fd.missing=MissingPolicy::ValidityMask;
  using F=Field<std::uint64_t,2,AoSoA<4>>;
  auto &f=s.add<F>(fd,2,"exact",1,{7,8},0,layout());f.write()(0,0)=UINT64_MAX;f.write()(0,1)=0;
  s.find("T").validity_field().write()(1)=0;f.write()(1,0)=9007199254740993ull;f.write()(1,1)=42;s.freeze();
  auto l=layout();std::swap(l.addresses[0],l.addresses[1]);l.domain=3;l.addresses.push_back({});l.addresses[2].entity=3;
  s.remap("T",l);auto v=s.get<F>("T").read();
  check(v(0,0)==9007199254740993ull && v(1,0)==UINT64_MAX,"exact AoSoA remap");
  check(v(1,1)==0 && s.find("T").validity_field().read()(3)==0,"component validity remap");
  check(v(2,0)==7 && v(2,1)==8,"new row initializers");
  EquationMatrix<> m;m.row_space=m.column_space=d.identity();m.rows=m.columns=1;m.offsets={0,1};m.indices={0};m.values={1};m.symmetric=true;
  m.row_order=EquationOrdering::Solver;rejects([&]{m.validate();});m.column_order=m.row_order;m.require(d,d);
  check(EquationMatrix<>::decode(m.encode()).row_order==EquationOrdering::Solver,"ordering wire");m.stage=EquationStage::Full;rejects([&]{m.validate();});
  std::cout<<"incremental checks="<<checks<<" PASS\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}

