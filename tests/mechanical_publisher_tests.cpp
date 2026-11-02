#include <sdb/results/mechanical_publishers.hpp>
#include <iostream>
using namespace sdb;
using namespace sdb::lifecycle;
int main(int argc,char **argv) {
    try {
        if(argc!=2) throw std::invalid_argument("scratch");
        ModelBuilder<double> b;
        for(unsigned n=0;n<4;++n) b.add_node(n+1,{double(n==1),double(n==2),double(n==3)});
        b.add_material({1,1,{1,100,0},0}); b.add_part(1);
        b.add_element(1,Topology::Tet4,{1,2,3,4},1,1);
        b.set_sample_points(1,{{1,1,1,0},{1,1,2,0}});
        CompiledModel<double> m({},b.finalize());m.descriptors.push_back(descriptor("C3D4",{}));
        for(unsigned n=0;n<4;++n) m.sources.add(true,{{"","",n+1},n+1,n,{}});
        m.sources.add(false,{{"","",1},1,0,{}});
        auto model=ResultModel::from(m,17);
        ResultFrame f; f.info.id=f.info.increment=1;f.info.time=.1;bind_result_frame(f,model);
        FieldDescriptor d;d.name="SECTION_S";d.location=FieldLocation::SectionPoint;
        std::array<double,2> values{5.,-7.};
        f.fields.push_back(section_point_field(m,d,std::span<const double>(values)));
        using namespace general_contact;
        Binding start{model.mesh,model.configuration,0,0,0},end{model.mesh,model.configuration,19,1,.1};
        Database<double> contact(start);
        Record<double> r;r.key={1,{FeatureKind::Node,4,0,1},{FeatureKind::Face,1,0,1},0};
        r.history.status=Status::Slip;r.history.normal={1.,0.,0.};r.history.accumulated_slip=.25;r.history.friction_work=2.;
        contact.begin_trial();contact.stage({r});contact.commit(end);
        append_contact_history(f,model,contact,end,3);
        auto path=std::filesystem::path(argv[1])/"contact-sections.sdbres";
        std::filesystem::create_directories(path.parent_path());std::filesystem::remove(path);
        { NativeResultStorage storage(path,model,false,false,native::default_limit,{},false,ResultFormat::TypedIndexed3);
          storage.append(f); }
        ResultReader reader(path);
        if(typed_values<double>(reader.typed_field(0,"SECTION_S"))!=std::vector<double>{5.,-7.} ||
           typed_values<double>(reader.typed_field(0,"CFRICTION_WORK"))!=std::vector<double>{2.})
            throw std::runtime_error("published value mismatch");
        auto result=reader.typed_field(0,"CSTATUS");
        auto a=result.typed->addresses.at(0);
        if(a.endpoint_b.local!=1 || a.contact_generation!=3 || typed_values<std::uint32_t>(result)[0]!=2)
            throw std::runtime_error("contact key/status mismatch");
        std::cout<<"section/contact publishers PASS\n";
    } catch(const std::exception &e) {std::cerr<<e.what()<<std::endl;return 1;}
}
