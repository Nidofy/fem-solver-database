#pragma once
#include "../material_abi.h"
#include "../types.hpp"
#include <array>
#include <cmath>
#include <map>
#include <set>
#include <span>

namespace sdb::physics {
// Registrations own copied metadata. The provider must remain loaded during evaluation.
class MaterialRegistry {
  public:
    struct Entry {
        std::string schema; uint32_t version,scalar,tangent_kind;
        std::vector<std::string> parameters,history;
        sdb_material_eval_v1 evaluate;
    };
    template<class T> struct Trial {
        std::array<T,6> stress;
        std::vector<T> history,tangent;
    };
    void add(const sdb_material_plugin_v1& p) {
        if(p.abi_version!=SDB_MATERIAL_ABI_V1 || p.struct_size<sizeof(p) || !p.algorithm ||
           !p.schema_id || !*p.schema_id || !p.schema_version || !p.evaluate ||
           (p.scalar!=SDB_MAT_F32 && p.scalar!=SDB_MAT_F64) || p.tangent_kind>SDB_MAT_GENERAL)
            throw std::invalid_argument("invalid material plugin contract");
        auto names=[](const char*const* input,uint32_t n) {
            if(n>65536 || (n && !input)) throw std::invalid_argument("invalid material layout");
            std::set<std::string> seen; std::vector<std::string> out;
            for(uint32_t i=0;i<n;++i) {
                if(!input[i] || !*input[i] || !seen.emplace(input[i]).second)
                    throw std::invalid_argument("invalid/duplicate material component name");
                out.emplace_back(input[i]);
            }
            return out;
        };
        Entry e{p.schema_id,p.schema_version,p.scalar,p.tangent_kind,names(p.parameter_names,p.parameter_count),
                names(p.history_names,p.history_count),p.evaluate};
        if(!entries_.emplace(std::make_pair(p.algorithm,p.scalar),std::move(e)).second)
            throw std::invalid_argument("duplicate material algorithm/precision");
    }
    const Entry& lookup(uint32_t algorithm,uint32_t scalar) const {return entries_.at({algorithm,scalar});}
    template<class T>
    Trial<T> trial(uint32_t algorithm,std::string_view schema,uint32_t version,std::span<const T> parameters,
                   const std::array<T,6>& strain,std::span<const T> committed,bool tangent=false) const {
        static_assert(std::is_same_v<T,float> || std::is_same_v<T,double>);
        const auto& e=lookup(algorithm,sizeof(T)==4?SDB_MAT_F32:SDB_MAT_F64);
        if(e.schema!=schema || e.version!=version || parameters.size()!=e.parameters.size() ||
           committed.size()!=e.history.size() || (tangent && e.tangent_kind==SDB_MAT_NO_TANGENT))
            throw std::invalid_argument("incompatible material trial");
        auto finite=[](const auto& a){for(auto x:a) if(!std::isfinite(x))throw std::domain_error("nonfinite material value");};
        finite(parameters);finite(strain);finite(committed);
        const T missing=std::numeric_limits<T>::quiet_NaN();
        Trial<T> out{};out.stress.fill(missing);
        out.history.assign(committed.size(),missing); if(tangent) out.tangent.assign(36,missing);
        if(e.evaluate(parameters.data(),strain.data(),committed.data(),out.stress.data(),out.history.data(),
                      tangent?out.tangent.data():nullptr)!=0)
            throw std::runtime_error("material trial failed");
        finite(out.stress);finite(out.history);finite(out.tangent);return out;
    }
  private:
    std::map<std::pair<uint32_t,uint32_t>,Entry> entries_;
};
}
