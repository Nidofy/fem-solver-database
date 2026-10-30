#define main dbf7_fixture_main
#include "snapshot/dbf7_contract_tests.cpp"
#undef main
int main() {
  auto d=space(); auto s=state(d); StateSession owner(s);
  auto escaped=[&]{auto lease=owner.write();return lease.field<Field<double>>("T");}();
  {auto reader=owner.read(); escaped.at(0)=999;
   std::cout<<"expired write view wrote during read lease="<<reader.field<Field<double>>("T").at(0)<<'\n';}
  SpectralSeries sp;sp.model=42;sp.equations=d.identity();sp.field=desc();sp.layout=layout();
  sp.unit.symbol="m";sp.unit.powers[0]=1;sp.coordinates={0};sp.real={1,2};sp.imaginary={0,0};
  sp.validate();std::cout<<"conflicting field K / unit m accepted\n";
  return 0;
}
