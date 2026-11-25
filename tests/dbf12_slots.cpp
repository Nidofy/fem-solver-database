#include <sdb/paged_slot_field.hpp>
#include <chrono>
#include <iostream>

using namespace sdb;
using Pool=PagedSlotField<double,3,AoSoA<4>,16>;
static_assert(std::is_nothrow_move_constructible_v<Pool>);
using Path=std::filesystem::path;
static void check(bool v,const char *msg) { if(!v) throw std::runtime_error(msg); }
template<class F> static void rejects(F f) {
    try { f(); } catch(const std::exception &) {return;}
    throw std::runtime_error("expected slot rejection");
}
static std::vector<char> bytes(const Path &p) {
    std::ifstream in(p,std::ios::binary); return {std::istreambuf_iterator<char>(in),{}};
}
static void persistence(const Path &dir,bool partition_fixture) {
    using Page=slot_detail::SlotPage<double,3,AoSoA<4>,16>;
    static_assert(alignof(Page)>=64);
    auto page=std::make_shared<Page>();
    check(reinterpret_cast<std::uintptr_t>(page->values.data())%64==0 &&
          reinterpret_cast<std::uintptr_t>(page->meta.data())%64==0,"actual page alignment");
    Pool source(98,2); auto original=source.allocate(1); source.set(original,0,9);
    Pool copy(source); copy.set(original,0,2); copy.erase(original);
    check(source.get(original)==9,"committed copy failed COW");
    source.begin_trial(); source.set(original,0,7);
    rejects([&]{Pool invalid(source);}); rejects([&]{copy=source;}); rejects([&]{source=copy;});
    Pool moved(std::move(source));
    auto empty=[&](Pool &from) {
        check(!from.in_trial() && from.size()==0 && !from.find(1),"moved-from pool invariant");
        from.begin_trial(); from.allocate(8); from.rollback();
        check(from.size()==0 && !from.find(8),"moved-from rollback");
        auto path=dir/"moved-from.slots"; from.save(path); from.restore(path);
        check(from.allocate(1)==SlotHandle{0,1},"moved-from reuse");
    };
    empty(source); check(moved.in_trial() && moved.get(original)==7,"move lost active trial");
    copy.begin_trial(); auto keep=copy.allocate(20); copy.set(keep,0,17);
    try {
        copy=std::move(moved);
        throw std::runtime_error("move into an active destination accepted");
    } catch(const std::logic_error &) {}
    check(copy.in_trial() && copy.get(keep)==17 && moved.in_trial() && moved.get(original)==7,
          "rejected move changed a trial");
    copy.rollback();
    copy=std::move(moved); empty(moved); copy.rollback(); check(copy.get(original)==9,"move assignment rollback");
    Pool p(98,2); std::vector<SlotHandle> h;
    for(unsigned i=0;i<80;++i) {h.push_back(p.allocate(i)); p.set(h.back(),2,i+0.25);}
    p.erase(h[3]); p.erase(h[70]); p.set_valid(h[64],false);
    auto first=dir/"before.slots", second=dir/"after.slots"; p.save(first);
    auto live=host_memory.live.load(); p.begin_trial();
    check(host_memory.live.load()==live,"trial copied aligned value pages");
    p.set(h[1],2,99); auto detached=host_memory.live.load()-live;
    check(detached==16*3*sizeof(double),"first mutation did not detach exactly one value page");
    p.set(h[2],0,11); check(host_memory.live.load()-live==detached,"same page detached twice");
    p.erase(h[34]); auto trial=p.allocate(500);
    check(trial.slot==34 && trial.generation==2,"trial free list");
    for(unsigned i=0;i<100;++i) p.allocate(800+i);
    rejects([&]{p.save(second);}); p.rollback(); p.save(second);
    check(bytes(first)==bytes(second) && p.get(h[1],2)==1.25 && p.get(h[34],2)==34.25 &&
        !p.valid(h[64]) && !p.find(500),"rollback exact checkpoint");
    PagedSlotField<double,3,SoA,7> restored(98,2); restored.restore(first);
    check(restored.get(h[79],2)==79.25 && !restored.valid(h[64]),"cross-layout checkpoint");
    auto a=p.allocate(500), b=restored.allocate(500);
    check(a==b && a.slot==70 && a.generation==2,"persisted free list");
    check(p.allocate(501)==restored.allocate(501) && restored.find(501)->slot==3,"second free-list entry");
    rejects([&]{p.get(h[70]);});
    for(unsigned i=0;i<10000;++i) {
        auto old=a; p.erase(a); a=p.allocate(1000+i); check(a.slot==old.slot && a.generation==old.generation+1,"ABA");
        rejects([&]{p.set(old,0,7);});
    }
    Pool wrong_schema(99,2); rejects([&]{wrong_schema.restore(first);});
    auto damaged=dir/"bad.slots"; auto content=bytes(first); content.back()^=1;
    { std::ofstream out(damaged,std::ios::binary); out.write(content.data(),content.size()); }
    rejects([&]{p.restore(damaged);}); check(p.find(10999)==a,"damaged restore mutated pool");
    p.begin_trial(); p.set(a,0,27); p.commit(); check(p.get(a)==27,"trial commit");
    // A sidecar binds the exact bytes of the parent. Test both real parent formats.
    std::vector<Path> parents{dir.parent_path()/"dbf9-multi-scratch"/"multi.sdb"};
    if(partition_fixture) parents.push_back(dir.parent_path()/"dbf11-parts");
    for(const auto &parent:parents) {
        auto path=parent;
        if(std::filesystem::is_directory(path)) {
            std::ifstream pointer(path/"current"); std::string name; pointer>>name;
            path=path/name/"4"/"run.sdbmanifest";
        }
        check(std::filesystem::is_regular_file(path),"missing parent fixture");
        auto side=dir/"bound.slots"; p.save_sidecar(side,path);
        Pool reader(98,2); reader.restore_sidecar(side,path);
        check(reader.get(a)==27,"parent binding round trip");
        rejects([&]{reader.restore_sidecar(side,first);}); check(reader.get(a)==27,"parent mismatch atomicity");
    }
}
static void exhausted(const Path &dir) {
    namespace n=lifecycle::native;
    // Schema, header counts, then one valid committed live row at the last generation.
    n::Writer w; w.integer<std::uint64_t>(98); w.integer<std::uint32_t>(2);
    w.integer<std::uint32_t>(static_cast<unsigned>(ScalarType::Float64)); w.integer<std::uint32_t>(3);
    w.integer<std::uint32_t>(1); w.integer(UINT32_MAX); w.integer(UINT32_MAX); w.integer<std::uint8_t>(1);
    w.integer<std::uint64_t>(42); w.integer<std::uint8_t>(1); for(int i=0;i<3;++i) w.scalar(double(i));
    auto path=dir/"last-generation.slots";
    n::atomic_stream(path,[&](auto &out) { n::write(out,n::pack(UINT64_C(0x31544f4c53424453))); n::record(out,2,w.bytes); });
    Pool p(98,2); p.restore(path); SlotHandle last{0,UINT32_MAX}; check(p.get(last,2)==2,"last generation");
    p.erase(last); p.save(path); p.restore(path);
    check(p.allocate(42)==SlotHandle{1,1},"exhausted slot reentered free list"); rejects([&]{p.get(last);});
}
static void duplicate_packet() {
    Pool sender(98,2),receiver(98,2);
    sender.allocate(1); sender.allocate(2);
    auto packet=sender.export_migration_packets([](auto){return true;});
    auto b=std::span(reinterpret_cast<std::uint8_t *>(packet.data()),packet.size());
    constexpr std::size_t header=36,row=65;
    check(b.size()==header+2*row+4,"packet layout");
    std::copy_n(b.begin()+header,8,b.begin()+header+row);
    auto sign=[&](std::size_t first,std::size_t end){
        auto crc=checkpoint_detail::crc(b.subspan(first,end-first));
        for(unsigned j=0;j<4;++j) b[end+j]=static_cast<std::uint8_t>(crc>>(8*j));
    };
    sign(header+row,header+2*row-4); sign(0,b.size()-4);
    auto keep=receiver.allocate(99); receiver.begin_trial();
    rejects([&]{receiver.import_migration_packets(packet);});
    check(receiver.size()==1 && receiver.domain_key(keep)==99 && receiver.allocate(1)==SlotHandle{1,1},"duplicate packet partial import");
    receiver.rollback();
}
static void migration() {
    constexpr unsigned keys=4096;
    std::vector<Pool> ranks; for(unsigned i=0;i<4;++i) ranks.emplace_back(77,3);
    for(unsigned k=0;k<keys;++k) {
        auto &p=ranks[k%4]; auto h=p.allocate(UINT64_C(0x100000000)+k);
        p.set(h,0,k+0.125); p.set(h,2,-double(k)); if(k%11==0) p.set_valid(h,false);
    }
    std::array<std::vector<std::byte>,4> transfers;
    auto start=std::chrono::steady_clock::now(); std::size_t moved=0;
    for(unsigned rank=0;rank<4;++rank) {
        auto &p=ranks[rank]; p.begin_trial();
        auto selected=[](auto key){return ((key>>2)&1)==0;};
        transfers[rank]=p.export_migration_packets(selected,true); moved+=transfers[rank].size();
        p.rollback(); p.begin_trial();
        check(transfers[rank]==p.export_migration_packets(selected,true),"deterministic selection/order after rollback");
    }
    for(unsigned r=0;r<4;++r) {
        auto &recipient=ranks[(r+1)%4]; auto &packet=transfers[r]; auto corrupt=packet;
        corrupt[corrupt.size()-10]^=std::byte{1};
        rejects([&]{recipient.import_migration_packets(corrupt);});
        // Repair the envelope to reach the final row CRC after prior allocations.
        auto view=std::span(reinterpret_cast<std::uint8_t *>(corrupt.data()),corrupt.size());
        auto crc=checkpoint_detail::crc(view.first(view.size()-4));
        for(unsigned j=0;j<4;++j) view[view.size()-4+j]=static_cast<std::uint8_t>(crc>>(8*j));
        auto before=recipient.size(); rejects([&]{recipient.import_migration_packets(corrupt);});
        check(recipient.size()==before,"failed import changed target");
        Pool wrong(77,4); rejects([&]{wrong.import_migration_packets(packet);});
        recipient.import_migration_packets(packet);
        rejects([&]{recipient.import_migration_packets(packet);});
    }
    for(auto &p:ranks) p.commit();
    for(unsigned k=0;k<keys;++k) {
        auto dest=(k%4+(((k>>2)&1)==0))%4; auto key=UINT64_C(0x100000000)+k;
        for(unsigned r=0;r<4;++r) {
            auto h=ranks[r].find(key); check(h.has_value()==(r==dest),"migration unique owner");
            if(h) check(ranks[r].get(*h)==k+0.125 && ranks[r].get(*h,2)==-double(k) &&
                        ranks[r].valid(*h)==(k%11!=0),"migration payload/validity");
        }
    }
    auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    start=std::chrono::steady_clock::now();
    for(auto &p:ranks) p.begin_trial();
    for(unsigned rank=0;rank<4;++rank)
        transfers[rank]=ranks[rank].export_migration_packets([](auto k){return ((k>>2)&1)==0;},true);
    for(unsigned rank=0;rank<4;++rank) ranks[(rank+3)%4].import_migration_packets(transfers[rank]);
    for(auto &p:ranks) p.commit();
    auto return_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    for(unsigned k=0;k<keys;++k) {
        auto &owner=ranks[k%4]; auto h=owner.find(UINT64_C(0x100000000)+k);
        check(h && owner.get(*h)==k+0.125 && owner.valid(*h)==(k%11!=0),"return trip lost state");
    }
    std::cout<<"return_migration_ms="<<return_ms<<" ranks=4 live_keys="<<keys<<" migrated="<<keys/2<<" packet_bytes="<<moved<<" migration_and_fault_checks_ms="<<ms<<"\n";
}
int main(int argc,char **argv) { try {
    if(argc!=3) return 2;
    Path dir=argv[1]; std::filesystem::create_directories(dir);
    auto initial=host_memory.live.load(); persistence(dir,std::string_view(argv[2])=="partition"); exhausted(dir); duplicate_packet(); migration();
    check(initial==host_memory.live.load(),"aligned page storage leaked");
    return 0;
} catch(const std::exception &e) {std::cerr<<e.what()<<"\n"; return 1;} }
