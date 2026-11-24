#pragma once
#include "../io/native.hpp"
#include "../io/compression.hpp"
#include "../io/crc64.hpp"
#include <array>

namespace sdb {
enum class SpectralCodec : std::uint32_t { Uncompressed, Rle, Zstd };
struct SpectralArchiveDescriptor {
    SampleAxisDescriptor axis;
    std::uint64_t dofs=0;
    std::uint32_t chunk_dofs=256;
    std::uint64_t field_schema=0;
    template<class F> void visit(F f) { f(axis,dofs,chunk_dofs,field_schema); }
    void validate() const {
        axis.validate();
        if(axis.version!=2 || !field_schema || !dofs || dofs>UINT64_MAX/16 ||
           !chunk_dofs || chunk_dofs>compression::hard_raw_limit/16)
            throw std::invalid_argument("spectral archive descriptor");
    }
};
using SpectralValue=std::array<double,2>;
struct SpectralSlice { SpectralSampleMetadata metadata; std::vector<SpectralValue> values; };

namespace spectral_detail {
namespace n=lifecycle::native;
inline constexpr std::uint64_t legacy_magic=UINT64_C(0x3252544350534453), magic=UINT64_C(0x3352544350534453), footer=UINT64_C(0x32444e4550534453);
inline constexpr std::size_t chunk_entry_size=24, entry_size=40, footer_size=32, metadata_limit=65536;
inline void require(bool v,const char *message) { if(!v) throw std::runtime_error(message); }
inline void value(const SampleAxisDescriptor &axis,const SpectralValue &v) {
    require(std::isfinite(v[0]) && std::isfinite(v[1]) &&
            (axis.channels!=ComplexChannels::MagnitudePhase || v[0]>=0),"invalid complex spectral value");
}
inline std::uint64_t position(std::ostream &out) {
    auto p=out.tellp(); require(p>=0,"spectral write position"); return static_cast<std::uint64_t>(p);
}
inline std::uint64_t position(std::istream &in) {
    auto p=in.tellg(); require(p>=0,"spectral read position"); return static_cast<std::uint64_t>(p);
}
inline void seek(std::istream &in,std::uint64_t pos) {
    require(pos<=static_cast<std::uint64_t>(INT64_MAX),"spectral seek overflow");
    in.seekg(static_cast<std::streamoff>(pos)); require(bool(in),"spectral seek failed");
}
template<std::size_t N> std::array<std::uint8_t,N> read_fixed(std::istream &in) {
    std::array<std::uint8_t,N> v; n::read(in,v); return v;
}
inline void write_checked(std::ostream &out,n::Writer &w) { w.integer(io::crc64(w.bytes)); n::write(out,w.bytes); }
inline n::Reader checked(std::span<const std::uint8_t> b) {
    auto data=b.first(b.size()-8);
    require(n::Reader{b.last(8)}.integer<std::uint64_t>()==io::crc64(data),"spectral directory/footer CRC64");
    return {data};
}
struct Block {
    SpectralCodec codec{};
    std::uint32_t raw=0,stored=0,crc32=0;
    std::uint64_t crc64=0;
    template<class F> void visit(F f) { f(codec,raw,stored,crc32,crc64); }
};
inline n::Bytes compress(SpectralCodec &codec,const n::Bytes &raw) {
    if(codec==SpectralCodec::Zstd) {
        auto block=compression::encode(raw,{compression::Codec::Zstd,3,0,false});
        return std::move(block.payload);
    }
    if(codec==SpectralCodec::Rle) {
        n::Bytes packed;
        for(std::size_t i=0;i<raw.size();) {
            std::size_t count=1;
            while(i+count<raw.size() && raw[i+count]==raw[i] && count<255) ++count;
            packed.push_back(static_cast<std::uint8_t>(count)); packed.push_back(raw[i]); i+=count;
            if(packed.size()>=raw.size()) break;
        }
        if(packed.size()<raw.size()) return packed;
    }
    codec=SpectralCodec::Uncompressed; return raw;
}
inline n::Bytes decompress(const Block &b,n::Bytes stored) {
    n::Bytes raw;
    if(b.codec==SpectralCodec::Uncompressed) {
        require(b.stored==b.raw,"spectral raw size"); raw=std::move(stored);
    } else if(b.codec==SpectralCodec::Zstd) {
        raw=compression::decode(compression::Codec::Zstd,stored,b.raw,b.crc32);
    } else {
        require(b.codec==SpectralCodec::Rle && stored.size()%2==0,"invalid spectral RLE framing");
        raw.reserve(b.raw);
        for(std::size_t i=0;i<stored.size();i+=2) {
            auto count=stored[i]; require(count && count<=b.raw-raw.size(),"invalid spectral RLE run");
            raw.insert(raw.end(),count,stored[i+1]);
        }
    }
    require(raw.size()==b.raw && io::crc64(raw)==b.crc64,"spectral chunk CRC64/length");
    return raw;
}
// One private directory per writer. Only files inside the successfully claimed
// directory belong to this object. A failed append never publishes the spool.
class Scratch {
  public:
    std::filesystem::path dir;
    explicit Scratch(const std::filesystem::path &target) {
        static std::atomic<std::uint64_t> serial{};
        for(;;) {
            dir=target;
            dir+=".spectral."+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"."+std::to_string(serial++);
            if(std::filesystem::create_directory(dir)) break;
        }
    }
    Scratch(const Scratch &)=delete;
    ~Scratch() {
        std::error_code ec;
        std::filesystem::remove(dir/"data",ec); std::filesystem::remove(dir/"index",ec);
        std::filesystem::remove(dir,ec);
    }
};
inline void copy(const std::filesystem::path &path,std::ostream &out) {
    std::ifstream in(path,std::ios::binary); require(bool(in),"spectral spool open failed");
    std::array<std::uint8_t,32768> buffer;
    while(in.read(reinterpret_cast<char *>(buffer.data()),buffer.size()) || in.gcount())
        n::write(out,std::span(buffer).first(static_cast<std::size_t>(in.gcount())));
    require(in.eof() && !in.bad(),"spectral spool read failed");
}
} // namespace spectral_detail

class StreamingSpectralWriter {
    std::filesystem::path path_;
    SpectralArchiveDescriptor desc_;
    SpectralCodec codec_;
    lifecycle::native::FileLease lease_;
    spectral_detail::Scratch scratch_;
    std::ofstream data_,index_;
    std::uint64_t samples_=0;
    double previous_=0;
    bool closed_=false;
  public:
    StreamingSpectralWriter(const std::filesystem::path &path,SpectralArchiveDescriptor desc,SpectralCodec codec=SpectralCodec::Uncompressed)
        :path_(path),desc_(std::move(desc)),codec_(codec),lease_(path),scratch_(path),
         data_(scratch_.dir/"data",std::ios::binary),index_(scratch_.dir/"index",std::ios::binary) {
        desc_.validate();
        if(codec>SpectralCodec::Zstd || (codec==SpectralCodec::Zstd && !compression::available(compression::Codec::Zstd)))
            throw std::invalid_argument("unsupported spectral codec");
        spectral_detail::require(data_ && index_,"cannot open spectral spool");
    }
    void append(const SpectralSampleMetadata &meta,std::span<const SpectralValue> values) {
        using namespace spectral_detail;
        if(closed_) throw std::logic_error("spectral writer closed");
        meta.validate(desc_.axis);
        require(values.size()==desc_.dofs && (!samples_ || meta.coordinate>previous_),"spectral sample shape/order");
        for(auto &v:values) value(desc_.axis,v);
        require(meta.participation_factors.size()<=(metadata_limit-48)/8,"spectral metadata limit");
        auto metadata=n::pack(meta,metadata_limit);
        closed_=true;
        auto start=position(data_); n::record(data_,1,metadata);
        std::vector<std::uint64_t> offsets;
        for(std::size_t i=0;i<values.size();) {
            offsets.push_back(position(data_)-start);
            auto count=std::min<std::size_t>(desc_.chunk_dofs,values.size()-i);
            n::Writer raw; raw.bytes.reserve(count*16);
            for(auto &v:values.subspan(i,count)) {raw.scalar(v[0]); raw.scalar(v[1]);}
            auto codec=codec_; auto stored=compress(codec,raw.bytes);
            Block block{codec,static_cast<std::uint32_t>(raw.bytes.size()),static_cast<std::uint32_t>(stored.size()),
                        compression::crc32(raw.bytes),io::crc64(raw.bytes)};
            n::record(data_,2,n::pack(block)); n::write(data_,stored); i+=count;
        }
        n::Writer location;
        for(std::size_t i=0;i<offsets.size();++i) {
            location.bytes.clear(); location.integer<std::uint64_t>(i); location.integer(offsets[i]); write_checked(data_,location);
        }
        n::Writer entry; entry.integer(samples_); entry.integer(start); entry.integer(position(data_)-start);
        entry.scalar(meta.coordinate); write_checked(index_,entry);
        ++samples_; previous_=meta.coordinate; closed_=false;
    }
    void finish() {
        using namespace spectral_detail;
        if(closed_) throw std::logic_error("spectral writer closed");
        closed_=true; data_.close(); index_.close();
        require(data_ && index_,"spectral spool close failed");
        n::atomic_stream(path_,[&](auto &out) {
            n::write(out,n::pack(magic)); n::record(out,0,n::pack(desc_,metadata_limit));
            copy(scratch_.dir/"data",out);
            n::Writer tail; tail.integer(footer); tail.integer(position(out)); tail.integer(samples_);
            copy(scratch_.dir/"index",out); write_checked(out,tail);
        });
    }
};

class StreamingSpectralReader {
    std::ifstream in_;
    SpectralArchiveDescriptor desc_;
    std::uint64_t start_=0, dir_=0, count_=0;
    std::size_t decoded_=0,max_raw_=0,headers_=0,seeks_=0;
    void seek_to(std::istream &stream,std::uint64_t pos) { ++seeks_; spectral_detail::seek(stream,pos); }
    bool indexed_=false;
    std::unique_ptr<spectral_detail::Scratch> legacy_scratch_;
    std::ifstream legacy_offsets_;
    std::uint64_t chunks() const { return 1+(desc_.dofs-1)/desc_.chunk_dofs; }
    struct Entry { std::uint64_t start,end; double coordinate; };
    Entry directory_record(std::uint64_t sample) {
        using namespace spectral_detail;
        auto bytes=read_fixed<entry_size>(in_); auto r=checked(bytes);
        require(r.integer<std::uint64_t>()==sample,"spectral directory ordinal");
        auto offset=r.integer<std::uint64_t>(),length=r.integer<std::uint64_t>(); auto x=r.scalar<double>();
        desc_.axis.validate_coordinate(x);
        require(offset<=dir_-start_ && length<=dir_-start_-offset,"spectral directory extent");
        return {start_+offset,start_+offset+length,x};
    }
    Entry entry(std::uint64_t sample) {
        if(sample>=count_) throw std::out_of_range("spectral sample");
        seek_to(in_,dir_+spectral_detail::entry_size*sample); return directory_record(sample);
    }
    void validate_directory() {
        using namespace spectral_detail;
        seek_to(in_,dir_); auto next=start_; double previous=0;
        for(std::uint64_t s=0;s<count_;++s) {
            const auto e=directory_record(s);
            require(e.start==next && e.end>e.start && (!s || e.coordinate>previous),"spectral directory order/coverage");
            next=e.end; previous=e.coordinate;
        }
        require(next==dir_,"spectral directory does not cover sample region");
    }
    std::uint64_t chunk_data_end(const Entry &e) const {
        if(!indexed_) return e.end;
        spectral_detail::require(chunks()<=(e.end-e.start)/spectral_detail::chunk_entry_size,"spectral chunk table extent");
        return e.end-chunks()*spectral_detail::chunk_entry_size;
    }
    std::uint64_t offset_entry(const Entry &e,std::uint64_t ordinal,std::uint64_t end) {
        using namespace spectral_detail;
        auto bytes=read_fixed<chunk_entry_size>(in_); auto r=checked(bytes);
        require(r.integer<std::uint64_t>()==ordinal,"spectral chunk ordinal");
        auto offset=r.integer<std::uint64_t>();
        require(offset<end-e.start,"spectral chunk offset");
        return e.start+offset;
    }
    spectral_detail::Block block_header(std::uint64_t num,std::uint64_t limit) {
        using namespace spectral_detail;
        ++headers_;
        auto b=n::unpack<Block>(n::record(in_,2,24)); auto pos=position(in_);
        require(b.codec<=SpectralCodec::Zstd && b.raw==num*16 && b.stored && b.stored<=compression::Limits{}.max_stored_bytes &&
                pos<=limit && b.stored<=limit-pos,"spectral chunk extent");
        return b;
    }
    // Older candidates stored variable-length chunks without offsets. Build a
    // disk-backed index once for those frequency archives, without decoding values.
    void index_legacy_frequencies() {
        using namespace spectral_detail;
        legacy_scratch_=std::make_unique<Scratch>(std::filesystem::temp_directory_path()/"sdb-legacy-frf");
        auto path=legacy_scratch_->dir/"index"; std::ofstream out(path,std::ios::binary);
        require(bool(out),"legacy spectral scratch open");
        for(std::uint64_t s=0;s<count_;++s) {
            auto e=entry(s); seek_to(in_,e.start); (void)n::record(in_,1,metadata_limit);
            for(std::uint64_t i=0;i<desc_.dofs;) {
                auto start=position(in_); auto num=std::min<std::uint64_t>(desc_.chunk_dofs,desc_.dofs-i);
                auto b=block_header(num,e.end); auto stop=position(in_)+b.stored;
                n::write(out,n::pack(std::pair(start,stop))); seek_to(in_,stop); i+=num;
            }
            require(position(in_)==e.end,"legacy spectral slice extent");
        }
        out.close(); require(bool(out),"legacy spectral scratch close"); legacy_offsets_.open(path,std::ios::binary);
        require(bool(legacy_offsets_),"legacy spectral scratch read");
    }
    template<class Emit> SpectralSampleMetadata read(std::uint64_t sample,std::optional<std::uint64_t> dof,Emit emit) {
        using namespace spectral_detail;
        auto e=entry(sample); seek_to(in_,e.start);
        auto meta=n::unpack<SpectralSampleMetadata>(n::record(in_,1,metadata_limit),metadata_limit);
        meta.validate(desc_.axis); require(meta.coordinate==e.coordinate,"spectral coordinate mismatch");
        auto first_block=position(in_), data_end=chunk_data_end(e),limit=data_end;
        require(first_block<data_end,"spectral sample has no chunks");
        std::uint64_t i=0;
        if(dof) {
            auto ordinal=*dof/desc_.chunk_dofs; i=ordinal*desc_.chunk_dofs;
            std::uint64_t start;
            if(indexed_) {
                seek_to(in_,data_end+ordinal*chunk_entry_size);
                start=offset_entry(e,ordinal,data_end);
                if(ordinal+1<chunks()) limit=offset_entry(e,ordinal+1,data_end);
            } else {
                // The constructor emitted exactly one 16-byte extent per verified chunk.
                seek_to(legacy_offsets_,16*(sample*chunks()+ordinal));
                auto extent=n::unpack<std::pair<std::uint64_t,std::uint64_t>>(read_fixed<16>(legacy_offsets_));
                start=extent.first; limit=extent.second;
            }
            require(first_block<=start && start<limit && limit<=data_end,"spectral indexed bounds");
            seek_to(in_,start);
        }
        std::vector<std::uint64_t> offsets;
        while(i<desc_.dofs) {
            if(indexed_ && !dof) offsets.push_back(position(in_));
            auto num=std::min<std::uint64_t>(desc_.chunk_dofs,desc_.dofs-i);
            auto b=block_header(num,limit);
            n::Bytes stored(b.stored); n::read(in_,stored);
            auto raw=decompress(b,std::move(stored)); ++decoded_; max_raw_=std::max(max_raw_,raw.size());
            n::Reader r{raw}; auto first=dof?*dof-i:0, last=dof?first+1:num;
            r.pos=static_cast<std::size_t>(first*16);
            for(auto j=first;j<last;++j) {
                SpectralValue v{r.scalar<double>(),r.scalar<double>()};
                value(desc_.axis,v); emit(v);
            }
            if(dof) break;
            i+=num;
        }
        require(position(in_)==limit,"spectral block or slice trailing bytes");
        if(!dof) {
            for(std::size_t j=0;j<offsets.size();++j)
                require(offset_entry(e,j,data_end)==offsets[j],"spectral table disagrees with actual chunk");
            require(position(in_)==e.end,"spectral slice trailing bytes");
        }
        return meta;
    }
  public:
    explicit StreamingSpectralReader(const std::filesystem::path &path):in_(path,std::ios::binary) {
        using namespace spectral_detail;
        auto prefix=read_fixed<8>(in_);
        auto format=n::Reader{prefix}.integer<std::uint64_t>(); indexed_=format==magic;
        require(indexed_ || format==legacy_magic,"spectral archive magic");
        desc_=n::unpack<SpectralArchiveDescriptor>(n::record(in_,0,metadata_limit),metadata_limit); desc_.validate();
        start_=position(in_); in_.seekg(0,std::ios::end);
        auto length=position(in_); require(length>=start_+footer_size,"spectral archive truncated");
        seek_to(in_,length-footer_size); auto bytes=read_fixed<footer_size>(in_); auto r=checked(bytes);
        require(r.integer<std::uint64_t>()==footer,"spectral footer magic");
        dir_=r.integer<std::uint64_t>(); count_=r.integer<std::uint64_t>();
        require(dir_>=start_ && dir_<=length-footer_size,"spectral directory position");
        auto space=length-footer_size-dir_;
        require(space%entry_size==0 && count_==space/entry_size,"spectral directory length");
        validate_directory();
        if(!indexed_ && count_ && desc_.axis.kind==SampleAxisKind::FrequencyHz) index_legacy_frequencies();
    }
    const SpectralArchiveDescriptor &descriptor() const { return desc_; }
    std::uint64_t samples() const { return count_; }
    std::size_t seek_count() const { return seeks_; }
    std::size_t chunk_headers_read() const { return headers_; }
    std::size_t decoded_chunks() const { return decoded_; }
    std::size_t largest_decoded_chunk() const { return max_raw_; }
    SpectralSlice slice(std::uint64_t sample) {
        SpectralSlice out;
        if(sample>=count_) throw std::out_of_range("spectral sample");
        // Grow only as verified chunks arrive, even for untrusted declared DOF counts.
        out.metadata=read(sample,{},[&](auto v){out.values.push_back(v);}); return out;
    }
    SpectralSlice slice_at(double coordinate) {
        desc_.axis.validate_coordinate(coordinate);
        std::uint64_t first=0,last=count_;
        while(first<last) {
            auto middle=first+(last-first)/2;
            if(entry(middle).coordinate<coordinate) first=middle+1;
            else last=middle;
        }
        if(first==count_ || entry(first).coordinate!=coordinate) throw std::out_of_range("spectral coordinate absent");
        return slice(first);
    }
    template<class Emit> void frequency_response(std::uint64_t dof,Emit emit) {
        if(desc_.axis.kind!=SampleAxisKind::FrequencyHz || dof>=desc_.dofs) throw std::invalid_argument("FRF axis or DOF");
        for(std::uint64_t s=0;s<count_;++s) {
            SpectralValue v{}; auto meta=read(s,dof,[&](auto x){v=x;}); emit(meta,v);
        }
    }
};
} // namespace sdb
