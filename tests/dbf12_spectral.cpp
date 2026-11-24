#include <sdb/results/streaming_spectral.hpp>
#include <sdb/process_memory.hpp>
#include <iostream>

using namespace sdb;
using Path=std::filesystem::path;
constexpr unsigned ndof=192,chunk=32;
static void check(bool v,const char *msg) { if(!v) throw std::runtime_error(msg); }
template<class F> static void rejects(F f) {
    try { f(); } catch(const std::exception &) {return;}
    throw std::runtime_error("expected spectral rejection");
}
static SpectralValue expected(unsigned s,unsigned i) {
    return i%5==0 ? SpectralValue{double((s+1)*(i+3)),double(s)/16-i} : SpectralValue{0,0};
}
static SpectralSampleMetadata metadata(unsigned s,bool modal) {
    return {modal?double(s+1):double(s)/8,double(s)-2,s*0.5,s+0.25,0.03125,{double(s),-double(s)}};
}
static void write(const Path &p,SpectralCodec codec,bool modal) {
    auto channels=modal?ComplexChannels::MagnitudePhase:ComplexChannels::RealImag;
    SampleAxisDescriptor axis{2,modal?SampleAxisKind::ModeNumber:SampleAxisKind::FrequencyHz,modal?"1":"Hz",channels};
    auto prior=process_memory::stats(); std::vector<char> floor(prior.peak-prior.live);
    auto baseline=process_memory::stats();
    StreamingSpectralWriter writer(p,{axis,ndof,chunk,17},codec);
    std::vector<SpectralValue> values(ndof);
    for(unsigned s=0;s<(modal?200u:500u);++s) {
        for(unsigned i=0;i<ndof;++i) values[i]=expected(s,i);
        auto m=metadata(s,modal); writer.append(m,values);
        if(!s) rejects([&]{writer.append(m,values);});
    }
    writer.finish();
    auto peak=process_memory::stats().peak-baseline.live;
    check(peak<256*1024,"writer buffered entire archive");
    std::cout<<p.filename().string()<<" writer_extra_peak_bytes="<<peak<<"\n";
}
static void read(const Path &p,bool modal) {
    namespace pm=process_memory;
    auto prior=pm::stats(); std::vector<char> floor(prior.peak-prior.live);
    auto baseline=pm::stats();
    StreamingSpectralReader reader(p); auto samples=modal?200u:500u;
    check(reader.samples()==samples && reader.decoded_chunks()==0,"reader eagerly decoded payload");
    for(auto s:{samples-1,0u,47u}) {
        auto slice=reader.slice(s); auto meta=metadata(s,modal);
        check(lifecycle::native::pack(slice.metadata)==lifecycle::native::pack(meta) && slice.values.size()==ndof,"slice metadata");
        for(unsigned i=0;i<ndof;++i) check(slice.values[i]==expected(s,i),"slice payload");
    }
    auto requested=metadata(49,modal).coordinate;
    check(reader.slice_at(requested).values[15]==expected(49,15),"axis lookup");
    rejects([&]{reader.slice_at(modal?501:requested+0.0625);});
    auto before=reader.decoded_chunks();
    if(!modal) {
        unsigned seen=0;
        reader.frequency_response(75,[&](const auto &m,auto v) {
            check(m.coordinate==double(seen)/8 && v==expected(seen,75),"single DOF FRF"); ++seen;
        });
        check(seen==samples && reader.decoded_chunks()-before==samples,"FRF read unrelated chunks");
    } else rejects([&]{reader.frequency_response(0,[](auto &,auto){});});
    rejects([&]{reader.slice(samples);});
    auto peak=pm::stats().peak-baseline.live;
    // Raw/RLE paths fit well below one archive; zstd's bounded decoder workspace
    // is separately capped by compression::Limits and depends on its runtime.
    if(p.extension()!=".zst") check(peak<128*1024,"reader memory scaled with archive");
    check(reader.largest_decoded_chunk()==chunk*16,"decoded chunk bound");
    std::cout<<p.filename().string()<<" samples="<<samples<<" bytes="<<std::filesystem::file_size(p)
             <<" reader_extra_peak_bytes="<<peak<<" largest_raw_chunk="<<reader.largest_decoded_chunk()
             <<" decoded_chunks="<<reader.decoded_chunks()<<"\n";
}
static void wide_frf(const Path &dir) {
    constexpr unsigned width=65537, per_block=32, points=4;
    auto path=dir/"wide.spectral";
    SampleAxisDescriptor axis{2,SampleAxisKind::FrequencyHz,"Hz",ComplexChannels::RealImag};
    { StreamingSpectralWriter writer(path,{axis,width,per_block,99},SpectralCodec::Rle);
      std::vector<SpectralValue> values(width);
      for(unsigned s=0;s<points;++s) {
          for(unsigned i=0;i<width;++i) values[i]=expected(s,i);
          writer.append(metadata(s,false),values);
      }
      writer.finish();
    }
    StreamingSpectralReader reader(path);
    for(unsigned dof:{0u,32770u,width-1}) {
        auto before=reader.chunk_headers_read(); unsigned s=0;
        reader.frequency_response(dof,[&](auto &,auto v){check(v==expected(s++,dof),"wide FRF data");});
        check(s==points && reader.chunk_headers_read()-before==points,"FRF scanned unrelated headers");
    }
    check(reader.slice(3).values.size()==width,"full indexed slice");
    std::cout<<"wide_dofs="<<width<<" chunks_per_sample="<<(1+(width-1)/per_block)
             <<" FRF_headers_per_sample=1 samples="<<points<<"\n";
}
static void faults(const Path &dir) {
    wide_frf(dir);
    auto original=dir/"modes.raw", path=dir/"damaged.spc";
    std::filesystem::copy_file(original,path,std::filesystem::copy_options::overwrite_existing);
    // Read real record headers to target the first payload without damaging its header.
    std::ifstream in(path,std::ios::binary); auto magic=spectral_detail::read_fixed<8>(in); (void)magic;
    lifecycle::native::record(in,0); lifecycle::native::record(in,1); lifecycle::native::record(in,2);
    auto pos=in.tellg(); char c=0; in.get(c); in.close();
    std::fstream out(path,std::ios::in|std::ios::out|std::ios::binary); out.seekp(pos); out.put(c^1); out.close();
    StreamingSpectralReader reader(path); rejects([&]{reader.slice(0);});
    check(reader.slice(1).values[0]==expected(1,0),"damage affected unrelated sample");
    auto size=std::filesystem::file_size(path);
    std::filesystem::resize_file(path,size-1); rejects([&]{StreamingSpectralReader partial(path);});
    for(auto axis:{SampleAxisKind::Time,SampleAxisKind::LoadFactor,SampleAxisKind::BucklingEigenvalue}) {
        SampleAxisDescriptor d{2,axis,"1",ComplexChannels::RealImag};
        d.validate_coordinate(axis==SampleAxisKind::Time ? 0 : -1);
    }
    SampleAxisDescriptor d{2,SampleAxisKind::ModeNumber,"1",ComplexChannels::RealImag};
    rejects([&]{d.validate_coordinate(1.5);});
}
int main(int argc,char **argv) { try {
    if(argc!=3) return 2;
    Path dir=argv[2]; std::string op=argv[1];
    std::filesystem::create_directories(dir);
    bool zstd=compression::available(compression::Codec::Zstd);
    if(op=="write") {
        write(dir/"modes.raw",SpectralCodec::Uncompressed,true);
        write(dir/"frequency.rle",SpectralCodec::Rle,false);
        if(zstd) write(dir/"frequency.zst",SpectralCodec::Zstd,false);
    } else if(op=="read") {
        read(dir/"modes.raw",true); read(dir/"frequency.rle",false);
        if(zstd) read(dir/"frequency.zst",false);
    } else if(op=="faults") faults(dir);
    else return 2;
    return 0;
} catch(const std::exception &e) {std::cerr<<e.what()<<"\n";return 1;} }
