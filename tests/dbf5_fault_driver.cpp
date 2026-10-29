#include <sdb/results/checkpoint_link.hpp>
#include <cstdlib>
#include <iostream>

using namespace sdb;
using namespace sdb::lifecycle;
namespace fs = std::filesystem;

struct Injection {
    durability::FaultPoint point;
    fs::path target;
    bool temporary = false;
};
void crash(durability::FaultPoint point, const fs::path &path, void *opaque) {
    const auto &in = *static_cast<Injection *>(opaque);
    const auto actual = path.lexically_normal().generic_string();
    const auto target = in.target.lexically_normal().generic_string();
    if (point == in.point && (actual == target || (in.temporary && actual.starts_with(target + ".tmp."))))
        std::_Exit(77); // Deliberately bypass stream flush/destructors in this child process.
}
durability::FaultPoint point(const std::string &name) {
    using P = durability::FaultPoint;
    const std::map<std::string, P> points{
        {"write",P::ResultWrite}, {"commit",P::IndexCommit}, {"temp",P::AfterTempWrite},
        {"sync-before",P::BeforeFileSync}, {"sync-after",P::AfterFileSync},
        {"replace-before",P::BeforeReplace}, {"replace-after",P::AfterReplace},
        {"parent-before",P::BeforeParentSync}, {"parent-after",P::AfterParentSync},
        {"link-before",P::BeforeLinkPublish}, {"link-after",P::AfterLinkPublish}};
    return points.at(name);
}
ResultFormat format(const std::string &name) {
    if (name == "native") return ResultFormat::Native1;
    if (name == "indexed") return ResultFormat::Indexed2;
    if (name == "typed") return ResultFormat::TypedIndexed3;
    if (name == "compressed") return ResultFormat::CompressedIndexed4;
    throw std::invalid_argument("format");
}
ResultFrame snapshot(CpuRun<double> &run, const CompiledModel<double> &model, ResultFormat f) {
    auto frame = run.snapshot();
    if (f == ResultFormat::TypedIndexed3 || f == ResultFormat::CompressedIndexed4)
        bind_result_frame(frame, ResultModel::from(model,run.configuration()));
    if (frame.fields.empty()) throw std::runtime_error("expected scheduled snapshot");
    return frame;
}
int main(int argc,char **argv) {
    try {
        if (argc < 3) throw std::invalid_argument("command directory [arguments]");
        const std::string command=argv[1];
        const fs::path dir=fs::absolute(argv[2]);
        const auto model_path=dir/"model.sdbmodel", result=dir/"result.sdbres", restart=dir/"state.sdbrst";
        if (command=="reject-codec") {
            if (compression::available(compression::Codec::Zstd)) throw std::runtime_error("expected no-zstd build");
            bool rejected=false;
            try { ResultReader reader(result); for (std::size_t i=0;i<reader.index.size();++i) (void)reader.frame(i); }
            catch (const std::exception &error) {
                const std::string message=error.what();
                if (message.find("zstd")==std::string::npos || message.find("unavailable")==std::string::npos) throw;
                rejected=true;
            }
            if (!rejected) throw std::runtime_error("compressed file accepted without codec");
            std::cout<<"fresh no-zstd child rejected compressed file\n"; return 0;
        }
        if (command=="init") {
            if (argc!=5) throw std::invalid_argument("init directory fixture format");
            fs::create_directories(dir);
            auto model=ModelCompiler{}.compile<double>(ReaderRegistry{}.read(argv[3]).model);
            ModelStore::save(model_path,model);
            CpuRun<double> run(model); run.initialize();
            const auto fmt=format(argv[4]);
            NativeResultStorage storage(result,ResultModel::from(model,run.configuration()),false,false,
                                        native::default_limit,{},false,fmt, fmt == ResultFormat::CompressedIndexed4 ? std::optional<CompressionSettings>{CompressionSettings{{compression::Codec::Zstd,3,0,false},{}}} : std::nullopt);
            storage.append(snapshot(run,model,fmt));
            storage.barrier(durability::Level::FileSync);
            // Deliberately create the legacy schema-1 link through the retained API.
            CheckpointLink::publish(result,restart,run,model);
            std::cout<<"legacy baseline ready\n";
            return 0;
        }
        auto model=ModelStore::load<double>(model_path);
        CpuRun<double> run(model); run.restore(restart);
        if (command=="inspect") {
            ResultReader reader(result,true);
            for (std::size_t i=0;i<reader.index.size();++i) (void)reader.frame(i);
            bool linked=true;
            try { CheckpointLink::verify(result,restart,run,model,true); }
            catch (const std::exception &) { linked=false; }
            if (linked && (reader.index.empty() || reader.index.back().time!=run.state().data().time))
                throw std::runtime_error("accepted link does not identify matching state/frame time");
            std::cout<<"{\"frames\":"<<reader.index.size()<<",\"step\":"<<run.state().data().step
                <<",\"linked\":"<<(linked?"true":"false")<<",\"tail\":"<<(reader.incomplete_tail?"true":"false")
                <<",\"valid_end\":"<<reader.valid_end<<"}\n";
            return 0;
        }
        if (command=="append-check") {
            if (argc!=4) throw std::invalid_argument("append-check directory recover");
            const bool recover=std::string(argv[3])=="yes";
            NativeResultStorage storage(result,ResultModel::from(model,run.configuration()),true,recover,
                native::default_limit,[&]{CheckpointLink::verify(result,restart,run,model,recover);});
            storage.barrier(durability::Level::FileSync);
            std::cout<<"append preflight and barrier accepted\n";
            return 0;
        }
        if (command!="mutate" || argc!=5) throw std::invalid_argument("mutate directory target point");
        const std::string target=argv[3];
        const auto fmt=ResultReader(result).format();
        NativeResultStorage storage(result,ResultModel::from(model,run.configuration()),true,false,
            native::default_limit,[&]{CheckpointLink::verify(result,restart,run,model);});
        while(run.state().data().step<100)
            if(!run.advance()) throw std::runtime_error("early end");
        Injection injection{point(argv[4]),target=="result"?result:target=="restart"?restart:CheckpointLink::path(restart),
                            target!="result"};
        durability::ScopedFaultHook hook({crash,&injection});
        storage.append(snapshot(run,model,fmt));
        CheckpointLink::publish_durable(storage,restart,run,model);
        throw std::runtime_error("fault point was not reached");
    } catch(const std::exception &e) {std::cerr<<e.what()<<'\n'; return 1;}
}

