#pragma once
#include "structure/formats/LitematicNbt.h"
#include <functional>
#include <array>
#include <type_traits>
#include <map>
#include <zlib.h>
namespace lholo::structure::detail {
class JavaNbtWriter {
    std::string bytes;
public:
    template<class T> void scalar(T value) {
        char raw[sizeof(T)];std::memcpy(raw,&value,sizeof(T));
        if constexpr(std::endian::native==std::endian::little)std::reverse(raw,raw+sizeof(T));
        bytes.append(raw,sizeof(T));
    }
    void string(std::string const& value) {
        if(value.size()>65535)throw std::runtime_error("NBT string exceeds 65535 bytes");
        scalar<std::uint16_t>(static_cast<std::uint16_t>(value.size()));bytes+=value;
    }
    void header(int type,std::string const& name){scalar<std::uint8_t>(type);string(name);}
    void payload(JavaNbtTag const& tag) {
        std::visit([&](auto const& value){
            using T=std::decay_t<decltype(value)>;
            if constexpr(std::is_same_v<T,std::monostate>) {}
            else if constexpr(std::is_arithmetic_v<T>)scalar<T>(value);
            else if constexpr(std::is_same_v<T,std::string>)string(value);
            else if constexpr(std::is_same_v<T,JavaNbtTag::Compound>){
                std::map<std::string,JavaNbtTag const*> sorted;for(auto const& [name,item]:value)sorted[name]=&item;
                for(auto const& [name,item]:sorted)named(name,*item);scalar<std::uint8_t>(0);
            }else if constexpr(std::is_same_v<T,JavaNbtTag::List>){
                int type=value.empty()?0:static_cast<int>(value.front().value.index());
                scalar<std::uint8_t>(type);scalar<std::int32_t>(static_cast<std::int32_t>(value.size()));
                for(auto const& item:value){if(static_cast<int>(item.value.index())!=type)throw std::runtime_error("Mixed NBT list");payload(item);}
            }else if constexpr(std::is_same_v<T,JavaNbtTag::LongArray>){
                scalar<std::int32_t>(static_cast<std::int32_t>(value.size()));bytes.append(value.bytes);
            }else{
                scalar<std::int32_t>(static_cast<std::int32_t>(value.size()));for(auto item:value)scalar(item);
            }
            if(bytes.size()>kMaximumJavaNbtSize)throw std::runtime_error("NBT exceeds size limit");
        },tag.value);
    }
    void named(std::string const& name,JavaNbtTag const& tag){header(static_cast<int>(tag.value.index()),name);payload(tag);}
    template<class F> void compound(std::string const& name,F body){header(10,name);body();scalar<std::uint8_t>(0);}
    void intTag(std::string const& name,std::int32_t value){header(3,name);scalar(value);}
    void longTag(std::string const& name,std::int64_t value){header(4,name);scalar(value);}
    void stringTag(std::string const& name,std::string const& value){header(8,name);string(value);}
    void xyz(std::string const& name,std::array<int,3> const& values){compound(name,[&]{intTag("x",values[0]);intTag("y",values[1]);intTag("z",values[2]);});}
    std::string take(){return std::move(bytes);}
};
inline std::string gzipJavaNbt(std::string const& bytes){
    z_stream stream{};
    if(deflateInit2(&stream,Z_DEFAULT_COMPRESSION,Z_DEFLATED,31,8,Z_DEFAULT_STRATEGY)!=Z_OK)throw std::runtime_error("Could not initialize gzip writer");
    struct End {z_stream* stream;~End(){deflateEnd(stream);}}end{&stream};
    stream.next_in=reinterpret_cast<Bytef*>(const_cast<char*>(bytes.data()));stream.avail_in=static_cast<uInt>(bytes.size());
    std::string output;std::array<char,65536> chunk;int result;
    do{stream.next_out=reinterpret_cast<Bytef*>(chunk.data());stream.avail_out=static_cast<uInt>(chunk.size());
        result=deflate(&stream,Z_FINISH);if(result!=Z_OK&&result!=Z_STREAM_END)throw std::runtime_error("gzip write failed");
        output.append(chunk.data(),chunk.size()-stream.avail_out);
    }while(result!=Z_STREAM_END);
    return output;
}
}
