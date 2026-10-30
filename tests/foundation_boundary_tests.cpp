#include <sdb/implicit.hpp>
#include <sdb/dynamics.hpp>
#include <iostream>
using namespace sdb;
unsigned checks = 0;
void check(bool ok) { ++checks; if (!ok) throw std::runtime_error("boundary check failed"); }
template <class F> void rejects(F f) {
    ++checks; try { f(); } catch (const std::exception &) { return; }
    throw std::runtime_error("boundary rejection expected");
}
template <class T> HistorySchema schema(std::string name, unsigned count) {
    HistorySchema s; s.id = name;
    HistoryVariable v; v.field.name = name; v.field.scalar = scalar_type<T>(); v.field.shape = {count};
    v.field.components.clear(); v.initial.clear();
    for (unsigned i = 0; i < count; ++i) {
        v.field.components.push_back(std::to_string(i)); v.initial.push_back(i + 1);
    }
    s.variables.push_back(v); return s;
}
template <class T> auto model(unsigned last_section = 1) {
    ModelBuilder<T> b;
    b.add_node(1, {0,0,0}, 6); b.add_node(2, {1,0,0}, 6);
    b.add_node(3, {1,1,0}, 6); b.add_node(4, {0,1,0}, 6); b.add_node(5, {0,0,1});
    b.add_material({1,42,{},1,schema<T>("A",1)});
    b.add_material({2,99,{},2,schema<T>("B",2)});
    b.add_material({3,1,{1,100,0},0}); b.add_part(1);
    b.add_element(1, Topology::Quad4, {1,2,3,4}, 1, 1);
    b.add_element(2, Topology::Beam2, {1,2}, 2, 1, 2);
    b.add_element(3, Topology::Tet4, {1,2,4,5}, 3, 1);
    b.set_sample_points(1, {{1,1,1,0},{1,1,2,0},{1,2,last_section,1}});
    b.set_storage_contract(123);
    return b.finalize();
}
template <class T> void layered(const std::filesystem::path &dir) {
    auto m = model<T>(); StateDB<T> s(m);
    check(m.requires_storage_only() && m.sample_point_count(0) == 3);
    check(s.ip_offsets().back() == 6 && s.history_offsets().back() == 8);
    check(s.point_history_offsets() == std::vector<std::size_t>({0,1,2,4,6,8,8}));
    check(s.history_address(m,0,0,"B",1,2,1) == 3);
    check(s.history_address_at(m,1,1,"B",1) == 7);
    check(s.data().history.read()(3) == T(2) && s.data().history.read()(7) == T(2));
    rejects([&] { s.history_address(m,0,0,"A"); });
    rejects([&] { s.history_address_at(m,0,2,"A"); });
    rejects([&] { CpuExplicit<T> run(m,s); });
    check(m.external_label(NodeId(1)) == ExternalLabel(2));
    check(m.node(ExternalLabel(5)) == NodeId(4));
    s.data().history.write()(3) = T(18); s.data().dt = .001;
    auto path = dir / (sizeof(T)==4 ? "layered-f.checkpoint" : "layered-d.checkpoint");
    save_checkpoint(path,m,s);
    StateDB<T> r(m); load_checkpoint(path,m,r);
    check(r.data().history.read()(3) == T(18));
    auto changed = model<T>(3); StateDB<T> bad(changed);
    check(changed.fingerprint() != m.fingerprint());
    rejects([&] { load_checkpoint(path,changed,bad); });
    check(bad.data().history.read()(3) == T(2));
}
template <class T> void matrix() {
    SparseMatrix<T,2> m(2,{0,2,4},{0,1,0,1});
    const T input[] = {2,0,0,3, 1,2,3,4, 1,3,2,4, 5,0,0,6};
    std::copy(std::begin(input),std::end(input),m.values().begin());
    m.set_symmetry(MatrixSymmetry::Symmetric);
    auto bytes = m.encode(); auto restored = SparseMatrix<T,2>::decode(bytes);
    std::array<T,4> x{1,2,3,4},y{},z{};
    m.apply(x,y); restored.apply(x,z);
    check(y==z && y[0]==T(13) && y[1]==T(31) && y[2]==T(22) && y[3]==T(34));
    check(restored.symmetry()==MatrixSymmetry::Symmetric);
    rejects([&] { m.encode(0); });
    auto d=m.data();d.index_bits=sizeof(Index)==4?64:32;rejects([&] { SparseMatrix<T,2>::from_data(d); });
    d=m.data();d.block_size=1;rejects([&] { SparseMatrix<T,2>::from_data(d); });
    d=m.data();d.offsets[1]=999;rejects([&] { SparseMatrix<T,2>::from_data(d); });
    d=m.data();d.columns[1]=0;rejects([&] { SparseMatrix<T,2>::from_data(d); });
    d=m.data();d.values[4]+=1;rejects([&] { SparseMatrix<T,2>::from_data(d); });
    bytes.pop_back();rejects([&] { SparseMatrix<T,2>::decode(bytes); });
    SparseMatrix<T> zero(0,{0},{});check(SparseMatrix<T>::decode(zero.encode()).size()==0);
}
int main(int argc,char**argv) {
    try {
        if(argc!=2) return 2;
        std::filesystem::create_directories(argv[1]);
        check(!NodeId{}.valid());check(NodeId::checked(0).valid());
        rejects([] {NodeId::checked(invalid_index);});
        auto wire=lifecycle::native::pack(NodeId(0x12345678));
        lifecycle::native::Bytes expected{0x78,0x56,0x34,0x12};expected.resize(sizeof(Index));
        check(wire==expected);
        check(lifecycle::native::unpack<NodeId>(wire)==NodeId(0x12345678));
        ModelBuilder<double> legacy_labels;legacy_labels.add_node(UINT64_MAX,{0,0,0});
        auto legacy=legacy_labels.finalize();check(legacy.node_id(0)==UINT64_MAX);
        rejects([&]{legacy.external_label(NodeId(0));});
        rejects([&]{legacy.node(ExternalLabel{});});
        MatrixData<double> malformed;malformed.block_rows=1;malformed.block_size=1024;
        malformed.offsets={0,1};malformed.columns={0};
        auto tiny=lifecycle::native::pack(malformed);auto allocations=host_memory.allocations.load();
        rejects([&]{SparseMatrix<double,1024>::decode(tiny,1024);});
        check(host_memory.allocations.load()==allocations);
        FieldRegistry registry; registry.add<Field<double>>("A",1);
        auto id=registry.field_id("A"); registry.get<Field<double>>(id).write()(0)=7;
        check(registry.get<Field<double>>("A").read()(0)==7 && registry.name(id)=="A");
        rejects([&] {registry.name(FieldId{});});
        rejects([&] {registry.add<Field<double>>("A",1);});
        registry.add<Field<double>>("B",1); check(registry.field_id("B").value()==1);
        layered<float>(argv[1]);layered<double>(argv[1]);matrix<float>();matrix<double>();
        std::cout<<checks<<" boundary/matrix/layer checks passed\n";
    } catch(const std::exception&e) { std::cerr<<e.what()<<'\n';return 1; }
}
