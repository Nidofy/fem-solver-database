#define main dbf7_fixture_main
#include "fixed-snapshot/dbf7_contract_tests.cpp"
#undef main
#include <sdb/lifecycle.hpp>
int main(int argc,char**argv) {try {
  if(argc!=3)return 2;
  auto d=space();auto s=state(d);StateSession owner(s);
  auto escaped=[&]{auto lease=owner.write();return lease.field<Field<double>>("T");}();
  {auto reader=owner.read(); rejects([&]{escaped.at(0)=999;});rejects([&]{escaped.raw();});
   check(reader.field<Field<double>>("T").at(0)==300,"unchanged under reader");}
  SpectralSeries sp;sp.model=42;sp.equations=d.identity();sp.field=desc();sp.layout=layout();
  sp.unit.symbol="m";sp.unit.powers[0]=1;sp.coordinates={0};sp.real={1,2};sp.imaginary={0,0};
  rejects([&]{sp.validate();});
  using namespace sdb::lifecycle;
  auto model=ModelCompiler{}.compile<double>(ReaderRegistry{}.read(argv[1]).model);
  CpuRun<double> run(model);auto rm=ResultModel::from(model,run.configuration());
  auto f=run.snapshot();f.fields.resize(1);f.fields[0].ids={1};f.fields[0].values={0,0,0};
  std::filesystem::path path=argv[2]; std::vector<double> times;
  {NativeResultStorage w(path,rm,false,false,native::default_limit,{},true);
   for(std::uint64_t i=0;i<129;++i){auto t=double(i)*.25+double(i%4)*.01; times.push_back(t);f.info.id=i;f.info.increment=i;f.info.time=t;w.append(f);}}
  ResultReader reader(path);
  for(double a=-1;a<=34;a+=.5)for(double b=a;b<=34;b+=.5){
    auto expected=std::pair<std::size_t,std::size_t>{std::lower_bound(times.begin(),times.end(),a)-times.begin(),std::upper_bound(times.begin(),times.end(),b)-times.begin()};
    check(reader.time_interval(a,b)==expected,"skip-link interval oracle");
  }
  std::cout<<"fixed probe checks="<<checks<<" PASS\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}

