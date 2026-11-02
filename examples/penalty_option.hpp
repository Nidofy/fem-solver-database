#pragma once
#include <sdb/model_ir/model_ir.hpp>

inline void apply_penalty_option(sdb::lifecycle::ModelIR& ir,const std::string& text) {
    std::size_t used=0;
    const double k=std::stod(text,&used);
    if(used!=text.size() || !std::isfinite(k) || k<=0)
        throw std::invalid_argument("penalty stiffness must be finite and positive");
    if(ir.foundation.equations.empty() && ir.foundation.rigids.empty() && ir.extensions.couplings.empty())
        throw std::invalid_argument("penalty stiffness requires an equation or coupling");
    ir.schema=std::max(ir.schema,6u);
    ir.penalty_stiffness=k;
}
