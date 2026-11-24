#include <sdb/integration_state.hpp>
#include <sdb/io/foundation_bundle.hpp>
#include "dbf9_batch_fixture.hpp"
#include <iostream>

using namespace sdb;
using Scalar=Field<double>;
using Tensor=Field<double,6,AoSoA<4>>;
static void check(bool v,const char *s) { if(!v) throw std::runtime_error(s); }
template<class F> static void rejects(F f) {
    try { f(); } catch(const std::exception &) { return; }
    throw std::runtime_error("invalid history accepted");
}
int main(int argc,char **argv) { try {
    if(argc!=3) return 2;
    auto dofs=dbf9_fixture::space();
    std::vector<ElementIntegration> elements={{3,1,{2,3}},{1,1,{}},{2,8,{}}};
    auto layout=integration_layout(elements);
    std::ranges::reverse(elements);
    check(integration_layout_identity(layout)==integration_layout_identity(integration_layout(elements)) &&
          layout.offsets==std::vector<std::uint64_t>{0,1,9,14},"deterministic integration layout");
    check(integration_row(layout,{3,1,2,3})==13 && integration_row(layout,{2,3})==3,"flat section/IP index");
    rejects([&]{integration_layout({{1,1,{}},{1,2,{}}});}); rejects([&]{integration_layout(elements,13);});
    auto fibers=integration_layout({{7,2,{1,1,1}}});
    check(integration_row(fibers,{7,2,3,1})==5,"beam fiber addressing");
    std::vector<consumer::v1::Batch> batches;
    const std::array<std::string,3> names{"C3D4","DC3D8","S4R"};
    for(std::size_t i=0;i<3;++i) {
        auto b=dbf9_fixture::data(dofs,i,names[i],i==1?8:4);
        b.samples.assign(layout.addresses.begin()+layout.offsets[i],layout.addresses.begin()+layout.offsets[i+1]);
        b.sample_offsets={0,b.samples.size()}; b.materials.assign(b.samples.size(),0); b.sections.assign(b.samples.size(),0);
        batches.push_back(dbf9_fixture::batch(dofs,std::move(b)));
    }
    BatchCatalog catalog(std::move(batches)); RegisteredState state;
    state.bind_context(dofs.data().model,dofs.identity(),catalog.identity());
    FieldDescriptor ep; ep.name="eq_plastic_strain"; ep.storage=StorageKind::Ragged;
    auto back=ep; back.name="backstress"; back.shape={6}; back.components={"11","22","33","12","13","23"};
    back.shear=ShearConvention::Tensor; back.missing=MissingPolicy::ValidityMask;
    HistorySchemaDescriptor history{"plugin.thermoplastic",3,{{ep,{0}},{back,{1,2,3,4,5,6},HistoryFinitePolicy::MaskMissing}}};
    rejects([&]{history.register_fields<Scalar,Scalar>(state,layout);}); check(state.empty(),"late schema failure changed registry");
    history.register_fields<Scalar,Tensor>(state,layout,77); state.freeze();
    if(std::string(argv[1])=="write") {
        state.get<Scalar>(ep.name).write()(13)=-0.0; state.get<Tensor>(back.name).write()(4,2)=12.25;
        state.get<Tensor>(back.name).write()(13,5)=0; state.find(back.name).validity_field().write()(13*6+5)=0;
        BundleCursor cursor; cursor.step=3; cursor.time=1.5; cursor.consumer_schema=history.id;
        MultiBatchBundle::save(argv[2],catalog,state,cursor);
    } else {
        auto info=MultiBatchBundle::inspect(argv[2]);
        check(info.catalog[1].data().formulation=="DC3D8" && info.fields[1].schema==history.id &&
              integration_layout_identity(info.fields[1].addressing)==integration_layout_identity(layout),"history metadata");
        auto cursor=MultiBatchBundle::restore(argv[2],catalog,state);
        check(cursor.step==3 && std::signbit(state.get<Scalar>(ep.name).read()(13)) &&
              state.get<Tensor>(back.name).read()(4,2)==12.25 && state.find(back.name).validity().back()==0,"history values or mask");
        RegisteredState wrong; wrong.bind_context(dofs.data().model,dofs.identity(),catalog.identity());
        ++history.version; history.register_fields<Scalar,Tensor>(wrong,layout,77); wrong.freeze();
        rejects([&]{MultiBatchBundle::restore(argv[2],catalog,wrong);}); check(wrong.get<Tensor>(back.name).read()(4,2)==3,"failed restore mutated history");
    }
    return 0;
} catch(const std::exception &e) { std::cerr<<e.what()<<'\n'; return 1; } }
