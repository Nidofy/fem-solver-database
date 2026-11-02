#include <sdb/lifecycle.hpp>
#include <iostream>
using namespace sdb::lifecycle;
int main(int argc,char** argv) {
    try {
        if(argc!=3) throw std::invalid_argument("fixture scratch");
        std::filesystem::create_directories(argv[2]);
        auto path=std::filesystem::path(argv[2])/"mapped.sdbres";
        std::filesystem::remove(path);
        auto model=ModelCompiler{}.compile<double>(ReaderRegistry{}.read(argv[1]).model);
        CpuRun<double> run(model);
        { NativeResultStorage storage(path,ResultModel::from(model,run.configuration()),false,false,
                                      native::default_limit,{},true);
          ResultWriter writer(storage);
          do {writer.write(run.snapshot());} while(run.advance());
        }
        ResultReader stream(path), mapped(path,false,native::default_limit,ResultReadMode::MappedImmutable);
        if(stream.index.size()!=mapped.index.size() || stream.index.size()<5) throw std::runtime_error("frame count");
        auto range=stream.time_interval(.03,.12);
        if(range!=mapped.time_interval(.03,.12) || range.first==range.second) throw std::runtime_error("time index");
        for(auto i=range.first;i<range.second;++i)
            if(native::pack(stream.frame(i))!=native::pack(mapped.frame(i))) throw std::runtime_error("mapped frame parity");
        std::cout<<"mapped interval and frame parity PASS\n";
    } catch(const std::exception &e) { std::cerr<<e.what()<<std::endl;return 1; }
}
