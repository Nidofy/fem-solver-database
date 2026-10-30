#pragma once
#include "../model_ir/foundation_validate.hpp"
#include <numbers>
#include <optional>
#include <fstream>
#include <charconv>
#include <cctype>

namespace sdb::lifecycle {
inline std::string trim(std::string s) {
    auto a = s.find_first_not_of(" \t\r\n");
    if (a == s.npos)
        return {};
    return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}
inline std::string upper(std::string s) {
    for (auto &c : s)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}
inline std::vector<std::string> csv(const std::string &s) {
    std::vector<std::string> r;
    std::string token;
    bool quoted = false;
    for (char c : s) {
        if (c == '"')
            quoted = !quoted;
        else if (c == ',' && !quoted) {
            r.push_back(trim(token));
            token.clear();
        } else
            token += c;
    }
    if (quoted)
        throw std::invalid_argument("unclosed quoted CSV value");
    r.push_back(trim(token));
    return r;
}
inline double number(std::string s, const Source &at) {
    for (auto &c : s)
        if (c == 'd' || c == 'D')
            c = 'E';
    std::size_t used = 0;
    double v;
    try {
        v = std::stod(s, &used);
    } catch (...) {
        fail(at, "invalid number: " + s);
    }
    if (used != s.size() || !std::isfinite(v))
        fail(at, "nonfinite/invalid number: " + s);
    return v;
}
inline Id label(const std::string &s, const Source &at) {
    Id v = 0;
    auto [p, e] = std::from_chars(s.data(), s.data() + s.size(), v);
    if (e != std::errc() || p != s.data() + s.size() || v == 0)
        fail(at, "positive integer label required: " + s);
    return v;
}
inline bool is_label(const std::string &s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c); });
}
struct ReaderOptions {
    ImportMode mode = ImportMode::Solve;
    std::filesystem::path allowed_root;
    std::size_t max_bytes = 256 * 1024 * 1024, max_depth = 16, max_entities = 10000000;
};
struct ReadResult {
    ModelIR model;
    ImportReport report;
};
class IModelReader {
  public:
    virtual ~IModelReader() = default;
    virtual bool probe(std::string_view extension, std::string_view prefix) const = 0;
    virtual ReadResult read(std::istream &, const std::filesystem::path &, ReaderOptions = {}) const = 0;
    ReadResult buffer(std::string_view s, const std::filesystem::path &name, ReaderOptions o = {}) const {
        std::istringstream in{std::string(s)};
        return read(in, name, std::move(o));
    }
};
class AbaqusInpReader final : public IModelReader {
    struct Line {
        std::string text;
        Source at;
    };
    struct Block {
        std::string key;
        std::map<std::string, std::string> params;
        std::vector<Line> data;
        Source at;
    };
    struct Session {
        ReaderOptions options;
        std::size_t bytes = 0, entities = 0;
        std::set<std::filesystem::path> active;
        std::vector<Line> lines(std::istream &in, const std::filesystem::path &p) {
            std::vector<Line> out;
            std::string s;
            std::uint64_t n = 0;
            while (std::getline(in, s)) {
                ++n;
                bytes += s.size() + 1;
                if (bytes > options.max_bytes)
                    fail({p.string(), n, 1}, "input byte budget exceeded");
                if (n == 1 && s.size() >= 3 && static_cast<unsigned char>(s[0]) == 0xef &&
                    static_cast<unsigned char>(s[1]) == 0xbb && static_cast<unsigned char>(s[2]) == 0xbf)
                    s.erase(0, 3);
                s = trim(s);
                if (s.empty() || s.starts_with("**"))
                    continue;
                out.push_back({s, {p.string(), n, 1}});
            }
            if (in.bad())
                fail({p.string(), n, 1}, "input read failed");
            return out;
        }
        std::filesystem::path resolve(const std::string &value, const Source &at) {
            auto raw = std::filesystem::path(value);
            if (raw.is_absolute())
                fail(at, "absolute external input path refused");
            auto path = std::filesystem::weakly_canonical(std::filesystem::path(at.file).parent_path() / raw);
            auto root = std::filesystem::weakly_canonical(options.allowed_root);
            auto rel = path.lexically_relative(root);
            if (rel.empty() || rel.is_absolute() || *rel.begin() == "..")
                fail(at, "external input escapes allowed root");
            return path;
        }
        std::vector<Line> external(const std::string &value, const Source &at) {
            auto p = resolve(value, at);
            std::ifstream in(p, std::ios::binary);
            if (!in)
                fail(at, "cannot open external input: " + p.string());
            return lines(in, p);
        }
        void blocks(std::istream &in, const std::filesystem::path &path, std::vector<Block> &out,
                    std::size_t depth = 0) {
            if (depth >= options.max_depth)
                fail({path.string(), 1, 1}, "include depth exceeded");
            auto canonical = std::filesystem::weakly_canonical(path);
            if (!active.insert(canonical).second)
                fail({path.string(), 1, 1}, "include cycle");
            auto rows = lines(in, path);
            for (std::size_t i = 0; i < rows.size();) {
                auto line = rows[i++];
                if (!line.text.starts_with('*'))
                    fail(line.at, "data outside keyword block");
                while (line.text.ends_with(',')) {
                    if (i == rows.size() || rows[i].text.starts_with('*'))
                        fail(line.at, "missing keyword continuation");
                    line.text += rows[i++].text;
                }
                auto fields = csv(line.text.substr(1));
                Block b;
                b.key = upper(fields[0]);
                b.at = line.at;
                for (std::size_t j = 1; j < fields.size(); ++j) {
                    auto pos = fields[j].find('=');
                    auto k = upper(trim(fields[j].substr(0, pos)));
                    auto v = pos == std::string::npos ? "" : trim(fields[j].substr(pos + 1));
                    if (k.empty() || !b.params.emplace(k, v).second)
                        fail(line.at, "empty/duplicate keyword parameter");
                }
                while (i < rows.size() && !rows[i].text.starts_with('*'))
                    b.data.push_back(rows[i++]);
                if (b.key == "INCLUDE") {
                    if (b.params.size() != 1 || !b.params.contains("INPUT") || !b.data.empty())
                        fail(b.at, "INCLUDE requires only INPUT and no data");
                    auto p = resolve(b.params.at("INPUT"), b.at);
                    std::ifstream nested(p, std::ios::binary);
                    if (!nested)
                        fail(b.at, "cannot open include: " + p.string());
                    blocks(nested, p, out, depth + 1);
                } else {
                    if (b.params.contains("INPUT")) {
                        if (b.key != "NODE")
                            fail(b.at, "keyword INPUT external data is supported only for NODE");
                        if (!b.data.empty())
                            fail(b.at, "NODE INPUT cannot mix inline data");
                        b.data = external(b.params.at("INPUT"), b.at);
                        for (auto &l : b.data)
                            if (l.text.starts_with('*'))
                                fail(l.at, "NODE INPUT must contain node data only");
                    }
                    out.push_back(std::move(b));
                }
            }
            active.erase(canonical);
        }
    };

  public:
    bool probe(std::string_view ext, std::string_view prefix) const override {
        return upper(std::string(ext)) == ".INP" || prefix.find("*NODE") != prefix.npos ||
               prefix.find("*Node") != prefix.npos || prefix.find("*HEADING") != prefix.npos;
    }
    ReadResult read(std::istream &in, const std::filesystem::path &path,
                    ReaderOptions options = {}) const override {
        if (options.allowed_root.empty())
            options.allowed_root = std::filesystem::absolute(path).parent_path();
        Session session{options, 0, 0, {}};
        std::vector<Block> blocks;
        session.blocks(in, std::filesystem::absolute(path), blocks);
        ReadResult result;
        auto &m = result.model;
        m.mode = options.mode;
        m.parts.push_back({"", {}, {}, {path.string(), 1, 1}});
        std::size_t part = 0;
        bool in_part = false, in_assembly = false, in_instance = false;
        IRMaterial *material = nullptr;
        IRStep *step = nullptr;
        bool history = false;
        bool output_pending = false, output_context = false;
        std::uint64_t frequency = 1, intervals = 0;
        std::string scope;
        std::optional<std::size_t> pending_coupling;
        std::uint32_t next_constraint=0;
        std::map<std::string,LoadId> pressure_ids;
        auto source_key=[&](std::string target,const Source &at) {
            SourceKey key; target=upper(target); auto dot=target.find('.');
            if(dot!=target.npos) {
                if(dot==0 || target.find('.',dot+1)!=target.npos) fail(at,"invalid scoped source node");
                key.instance=target.substr(0,dot); target=target.substr(dot+1);
            }
            if(!is_label(target)) fail(at,"node sets are outside this keyword's controlled subset");
            key.label=label(target,at); return key;
        };
        auto unsupported = [&](const Block &b, const std::string &why) {
            if (options.mode == ImportMode::Solve)
                fail(b.at, why);
            OpaqueBlock opaque{b.key, {}, b.at};
            for (auto &[k, v] : b.params)
                opaque.keyword += "," + k + "=" + v;
            for (auto &l : b.data)
                opaque.data.push_back(l.text);
            m.opaque.push_back(std::move(opaque));
            result.report.diagnostics.push_back({b.at, why, true});
        };
        for (auto &b : blocks) {
            if(pending_coupling && b.key!="KINEMATIC") fail(b.at,"COUPLING must immediately precede supported KINEMATIC");
            if ((b.key == "OUTPUT" || b.key == "END STEP") && output_pending)
                fail(b.at, "OUTPUT must be followed by explicit variable requests (including FREQUENCY=0)");
            auto param = [&](const std::string &key, const std::string &fallback = "") {
                auto it = b.params.find(key);
                return it == b.params.end() ? fallback : upper(it->second);
            };
            auto required = [&](const std::string &key) {
                auto s = param(key);
                if (s.empty())
                    fail(b.at, "missing parameter " + key);
                return s;
            };
            auto params = [&](std::initializer_list<const char *> names) {
                for (auto &[k, v] : b.params) {
                    (void)v;
                    bool ok = false;
                    for (auto n : names)
                        if (k == n)
                            ok = true;
                    if (!ok)
                        fail(b.at, "unsupported parameter " + k + " on *" + b.key);
                }
            };
            auto one = [&]() -> const Line & {
                if (b.data.size() != 1)
                    fail(b.at, "expected exactly one data line on *" + b.key);
                return b.data[0];
            };
            auto fields = [&](const Line &l) {
                try {
                    return csv(l.text);
                } catch (const std::exception &e) {
                    fail(l.at, e.what());
                }
            };
            auto count = [&](const std::vector<std::string> &f, std::size_t lo, std::size_t hi,
                             const Source &at) {
                if (f.size() < lo || f.size() > hi)
                    fail(at, "wrong data column count on *" + b.key);
            };
            auto add_members = [&](bool nodes, const std::string &name,
                                   const std::vector<std::string> &members, const Source &at,
                                   const std::string &instance = "") {
                if (!name.empty())
                    m.sets.push_back({scope, name, instance, nodes, members, at});
            };
            for (auto flag : {"GENERATE", "EXPLICIT", "DIRECT USER CONTROL", "FIELD", "HISTORY"})
                if (b.params.contains(flag) && !b.params.at(flag).empty())
                    fail(b.at, "flag cannot have a value: " + std::string(flag));
            if ((b.key == "PART" || b.key == "END PART" || b.key == "ASSEMBLY" || b.key == "END ASSEMBLY" ||
                 b.key == "END INSTANCE" || b.key == "MATERIAL" || b.key == "STEP" || b.key == "END STEP") &&
                !b.data.empty())
                fail(b.data.front().at, "unexpected data on structural keyword");
            if (b.key == "HEADING") {
                params({});
                continue;
            }
            if (b.key == "PART") {
                params({"NAME"});
                if (in_part || in_assembly || step)
                    fail(b.at, "invalid PART nesting");
                scope = required("NAME");
                m.parts.push_back({scope, {}, {}, b.at});
                part = m.parts.size() - 1;
                in_part = true;
            } else if (b.key == "END PART") {
                params({});
                if (!in_part)
                    fail(b.at, "END PART without PART");
                part = 0;
                scope.clear();
                in_part = false;
            } else if (b.key == "ASSEMBLY") {
                params({"NAME"});
                if (in_part || in_assembly || step || !m.assembly.empty())
                    fail(b.at, "invalid/duplicate ASSEMBLY");
                m.assembly = required("NAME");
                in_assembly = true;
                scope.clear();
            } else if (b.key == "END ASSEMBLY") {
                params({});
                if (!in_assembly || in_instance)
                    fail(b.at, "invalid END ASSEMBLY");
                in_assembly = false;
            } else if (b.key == "INSTANCE") {
                params({"NAME", "PART"});
                if (!in_assembly || in_instance)
                    fail(b.at, "INSTANCE requires ASSEMBLY");
                in_instance = true;
                IRInstance v;
                v.name = required("NAME");
                v.part = required("PART");
                v.source = b.at;
                if (b.data.size() > 2)
                    fail(b.at, "invalid instance transform");
                if (!b.data.empty()) {
                    auto f = fields(b.data[0]);
                    count(f, 3, 3, b.data[0].at);
                    for (int c = 0; c < 3; ++c)
                        v.translation[c] = number(f[c], b.data[0].at);
                }
                if (b.data.size() == 2) {
                    auto f = fields(b.data[1]);
                    count(f, 7, 7, b.data[1].at);
                    for (int c = 0; c < 7; ++c)
                        v.rotation[c] = number(f[c], b.data[1].at);
                }
                m.instances.push_back(std::move(v));
            } else if (b.key == "END INSTANCE") {
                params({});
                if (!in_instance)
                    fail(b.at, "END INSTANCE without INSTANCE");
                in_instance = false;
            } else if (b.key == "NODE") {
                params({"NSET", "INPUT"});
                if (in_instance || in_assembly || step)
                    fail(b.at, "NODE scope unsupported");
                for (auto &l : b.data) {
                    auto f = fields(l);
                    count(f, 2, 4, l.at);
                    IRNode n;
                    n.label = label(f[0], l.at);
                    n.source = l.at;
                    for (std::size_t c = 1; c < f.size(); ++c)
                        n.x[c - 1] = f[c].empty() ? 0 : number(f[c], l.at);
                    m.parts[part].nodes.push_back(n);
                    add_members(true, param("NSET"), {f[0]}, l.at);
                }
            } else if (b.key == "ELEMENT") {
                params({"TYPE", "ELSET"});
                if (in_instance || in_assembly || step)
                    fail(b.at, "ELEMENT scope unsupported");
                auto d = descriptor(required("TYPE"), b.at);
                for (std::size_t k = 0; k < b.data.size(); ++k) {
                    auto at = b.data[k].at;
                    auto f = fields(b.data[k]);
                    while (!f.empty() && f.back().empty())
                        f.pop_back();
                    while (f.size() < arity(d.shape) + 1 && k + 1 < b.data.size()) {
                        auto more = fields(b.data[++k]);
                        while (!more.empty() && more.back().empty())
                            more.pop_back();
                        f.insert(f.end(), more.begin(), more.end());
                    }
                    count(f, arity(d.shape) + 1, arity(d.shape) + 1, at);
                    IRElement e;
                    e.label = label(f[0], at);
                    e.descriptor = d;
                    e.source = at;
                    for (std::size_t c = 1; c < f.size(); ++c)
                        e.nodes.push_back(label(f[c], at));
                    m.parts[part].elements.push_back(e);
                    add_members(false, param("ELSET"), {f[0]}, at);
                }
            } else if (b.key == "NSET" || b.key == "ELSET") {
                if (b.key == "NSET")
                    params({"NSET", "GENERATE", "INSTANCE"});
                else
                    params({"ELSET", "GENERATE", "INSTANCE"});
                if (in_instance || step)
                    fail(b.at, "set scope unsupported");
                bool nodes = b.key == "NSET";
                std::vector<std::string> members;
                for (auto &l : b.data) {
                    auto f = fields(l);
                    if (b.params.contains("GENERATE")) {
                        count(f, 2, 3, l.at);
                        auto start = label(f[0], l.at), end = label(f[1], l.at),
                             inc = f.size() == 3 && !f[2].empty() ? label(f[2], l.at) : 1;
                        if (end < start || (end - start) % inc)
                            fail(l.at, "invalid GENERATE range");
                        if ((end - start) / inc >= options.max_entities)
                            fail(l.at, "GENERATE budget");
                        auto generated = (end - start) / inc + 1;
                        if (generated > options.max_entities - session.entities)
                            fail(l.at, "total GENERATE entity budget");
                        session.entities += static_cast<std::size_t>(generated);
                        for (auto v = start;; v += inc) {
                            members.push_back(std::to_string(v));
                            if (v == end)
                                break;
                        }
                    } else
                        for (auto &v : f)
                            if (!v.empty())
                                members.push_back(upper(v));
                }
                add_members(nodes, required(nodes ? "NSET" : "ELSET"), members, b.at, param("INSTANCE"));
            } else if (b.key == "COUPLING") {
                params({"CONSTRAINT NAME","REF NODE","SURFACE"});
                if(in_part || in_instance || step || !b.data.empty()) fail(b.at,"COUPLING supports root/assembly and no data lines");
                IRCoupling c; c.id=ConstraintId(next_constraint++); c.name=required("CONSTRAINT NAME");
                c.reference=source_key(required("REF NODE"),b.at); c.surface=required("SURFACE"); c.source=b.at;
                c.dof_mask=0; m.extensions.couplings.push_back(c); pending_coupling=m.extensions.couplings.size()-1; m.schema=4;
            } else if (b.key == "KINEMATIC") {
                params({});
                if(!pending_coupling) fail(b.at,"KINEMATIC without COUPLING");
                auto &c=m.extensions.couplings.at(*pending_coupling);
                if(b.data.empty()) fail(b.at,"KINEMATIC subset requires explicit translational DOFs");
                for(const auto &l:b.data) {
                    auto f=fields(l); count(f,1,2,l.at);
                    auto first=label(f[0],l.at),last=f.size()==1 || f[1].empty()?first:label(f[1],l.at);
                    if(first>last || last>3) fail(l.at,"KINEMATIC input subset supports DOFs 1..3");
                    for(auto d=first;d<=last;++d) {auto bit=static_cast<std::uint8_t>(1u<<(d-1));if(c.dof_mask&bit) fail(l.at,"duplicate KINEMATIC DOF");c.dof_mask|=bit;}
                }
                pending_coupling.reset();
            } else if (b.key == "INITIAL CONDITIONS") {
                params({"TYPE"});
                if(step || in_part || in_instance || in_assembly || required("TYPE")!="VELOCITY" || b.data.empty()) fail(b.at,"INITIAL CONDITIONS supports model-level VELOCITY only");
                for(const auto &l:b.data) {
                    auto f=fields(l); count(f,3,3,l.at); auto dof=label(f[1],l.at); if(dof>6) fail(l.at,"initial velocity DOF must be 1..6");
                    IRInitialCondition i; i.id=m.foundation.initial_conditions.size(); i.node=source_key(f[0],l.at); i.dof=static_cast<Dof>(dof-1); i.value=number(f[2],l.at); i.source=l.at;
                    m.foundation.initial_conditions.push_back(i);
                }
                m.schema=std::max(m.schema,3u);
            } else if (b.key == "DSLOAD") {
                params({"OP","AMPLITUDE"});
                if(!step || in_part || in_assembly || param("OP","MOD")!="MOD" || b.data.empty()) fail(b.at,"DSLOAD supports step pressure with OP=MOD only");
                for(const auto &l:b.data) {
                    auto f=fields(l); count(f,3,3,l.at); if(upper(f[1])!="P") fail(l.at,"DSLOAD supports uniform pressure P only");
                    auto region=upper(f[0]); auto [it,inserted]=pressure_ids.emplace(region,LoadId(index_of(pressure_ids.size()))); (void)inserted;
                    LoadDefinition d{it->second,StepId(index_of(m.steps.size()-1)),region,param("AMPLITUDE"),CoordinateId(0),true,LoadUpdate::Replace,l.at};
                    m.foundation.face_loads.push_back({d,FaceLoadKind::Pressure,number(f[2],l.at),{}});
                }
                m.schema=std::max(m.schema,3u);
            } else if (b.key == "EQUATION") {
                params({});
                if(in_part || in_instance || step) fail(b.at,"EQUATION supports root/assembly scope only");
                m.schema=std::max(m.schema,3u);
                if(b.data.empty()) fail(b.at,"empty EQUATION");
                for(std::size_t row=0;row<b.data.size();) {
                    auto f=fields(b.data[row]); count(f,1,1,b.data[row].at);
                    auto n=label(f[0],b.data[row].at);
                    if(n<2 || n>options.max_entities) fail(b.data[row].at,"equation term count/budget");
                    IREquation e; e.id=ConstraintId(next_constraint++); e.source=b.data[row++].at;
                    while(e.terms.size()<n) {
                        if(row==b.data.size()) fail(e.source,"truncated equation terms");
                        auto at=b.data[row].at; auto terms=fields(b.data[row++]);
                        while(!terms.empty() && terms.back().empty()) terms.pop_back();
                        if(terms.empty() || terms.size()>12 || terms.size()%3 || terms.size()/3>n-e.terms.size())
                            fail(at,"equation requires at most four node/DOF/coefficient triples per line");
                        for(std::size_t t=0;t<terms.size();t+=3) {
                            IREquationTerm term;
                            auto target=upper(terms[t]); auto dot=target.find('.');
                            if(dot!=target.npos) {
                                if(dot==0 || target.find('.',dot+1)!=target.npos) fail(at,"invalid equation scoped node");
                                term.node.instance=target.substr(0,dot); target=target.substr(dot+1);
                            }
                            if(!is_label(target)) fail(at,"equation node sets are outside the controlled subset");
                            term.node.label=label(target,at);
                            auto d=label(terms[t+1],at); if(d>6) fail(at,"equation DOF must be 1..6");
                            term.dof=static_cast<Dof>(d-1); term.coefficient=number(terms[t+2],at);
                            e.terms.push_back(std::move(term));
                        }
                    }
                    m.foundation.equations.push_back(std::move(e));
                }
            } else if (b.key == "ORIENTATION") {
                params({"NAME","SYSTEM","DEFINITION"});
                if(in_part || in_instance || in_assembly || step || param("SYSTEM","RECTANGULAR")!="RECTANGULAR" || param("DEFINITION","COORDINATES")!="COORDINATES")
                    fail(b.at,"ORIENTATION supports root rectangular coordinate definitions only");
                if(b.data.empty() || b.data.size()>2) fail(b.at,"orientation requires one or two data lines");
                auto f=fields(b.data[0]); while(!f.empty() && f.back().empty()) f.pop_back();
                if(f.size()!=6 && f.size()!=9) fail(b.at,"orientation requires points a,b and optional c");
                std::array<double,3> a{},v{},origin{},z{};
                for(unsigned k=0;k<3;++k) {
                    if(f.size()==9) origin[k]=f[k+6].empty()?0:number(f[k+6],b.at);
                    a[k]=(f[k].empty()?0:number(f[k],b.at))-origin[k];
                    v[k]=(f[k+3].empty()?0:number(f[k+3],b.at))-origin[k];
                }
                auto normalize=[&](auto &x) { double norm=std::hypot(x[0],x[1],x[2]); if(!(norm>0) || !std::isfinite(norm)) fail(b.at,"degenerate orientation points"); for(auto &q:x) q/=norm; };
                normalize(a); normalize(v);
                for(unsigned k=0;k<3;++k) z[k]=a[(k+1)%3]*v[(k+2)%3]-a[(k+2)%3]*v[(k+1)%3];
                normalize(z);
                for(unsigned k=0;k<3;++k) v[k]=z[(k+1)%3]*a[(k+2)%3]-z[(k+2)%3]*a[(k+1)%3];
                IROrientation o; o.id=OrientationId(index_of(m.foundation.orientations.size())); o.name=required("NAME"); o.source=b.at;
                for(unsigned k=0;k<3;++k) { o.rotation[k*3]=a[k]; o.rotation[k*3+1]=v[k]; o.rotation[k*3+2]=z[k]; }
                if(b.data.size()==2) {
                    auto r=fields(b.data[1]); count(r,1,2,b.data[1].at);
                    auto axis=r[0].empty()?1:label(r[0],b.at); if(axis>3) fail(b.at,"orientation rotation axis must be 1..3");
                    double angle=r.size()==1 || r[1].empty()?0:number(r[1],b.at)*std::numbers::pi/180;
                    Rotation extra=identity_rotation; auto j=static_cast<unsigned>(axis%3),k=static_cast<unsigned>((axis+1)%3);
                    extra[j*3+j]=extra[k*3+k]=std::cos(angle); extra[j*3+k]=-std::sin(angle); extra[k*3+j]=std::sin(angle);
                    o.rotation=multiply_rotation(o.rotation,extra);
                }
                validate_rotation(o.rotation,b.at); m.schema=std::max(m.schema,3u); m.foundation.orientations.push_back(std::move(o));
            } else if (b.key == "MATERIAL") {
                params({"NAME"});
                if (in_part || in_instance || step)
                    fail(b.at, "MATERIAL must be model-level");
                m.materials.push_back({required("NAME"), 0, 0, 0, b.at});
                material = &m.materials.back();
            } else if (b.key == "DENSITY" || b.key == "ELASTIC") {
                if (step || in_part || in_assembly)
                    fail(b.at, "material property must be model-level");
                params(b.key == "ELASTIC" ? std::initializer_list<const char *>{"TYPE"}
                                          : std::initializer_list<const char *>{});
                if (!material)
                    fail(b.at, "material property without MATERIAL");
                if (param("TYPE", "ISOTROPIC") != "ISOTROPIC")
                    fail(b.at, "only isotropic elasticity supported");
                auto &l = one();
                auto f = fields(l);
                while (!f.empty() && f.back().empty())
                    f.pop_back();
                count(f, b.key == "DENSITY" ? 1 : 2, b.key == "DENSITY" ? 1 : 2, l.at);
                if (b.key == "DENSITY") {
                    if (material->density)
                        fail(b.at, "duplicate density");
                    material->density = number(f[0], l.at);
                } else {
                    if (material->young)
                        fail(b.at, "duplicate elasticity");
                    material->young = number(f[0], l.at);
                    material->poisson = number(f[1], l.at);
                }
            } else if (b.key == "SOLID SECTION" || b.key == "SHELL SECTION" || b.key == "BEAM SECTION") {
                if (b.key == "BEAM SECTION")
                    params({"ELSET", "MATERIAL", "SECTION"});
                else
                    params({"ELSET", "MATERIAL"});
                if (step || in_instance)
                    fail(b.at, "invalid section scope");
                IRSection s{scope, required("ELSET"), required("MATERIAL"), b.key, {}, b.at};
                if (b.params.contains("SECTION"))
                    s.kind += "/" + param("SECTION");
                for (auto &l : b.data)
                    for (auto &v : fields(l))
                        if (!v.empty())
                            s.properties.push_back(number(v, l.at));
                m.sections.push_back(std::move(s));
            } else if (b.key == "SURFACE") {
                params({"NAME", "TYPE"});
                if (in_instance || step)
                    fail(b.at, "invalid surface scope");
                if (param("TYPE", "ELEMENT") != "ELEMENT")
                    fail(b.at, "only element surfaces supported");
                IRSurface s{scope, required("NAME"), {}, b.at};
                for (auto &l : b.data) {
                    auto f = fields(l);
                    count(f, 2, 2, l.at);
                    s.faces.emplace_back(upper(f[0]), upper(f[1]));
                }
                m.surfaces.push_back(std::move(s));
            } else if (b.key == "AMPLITUDE") {
                params({"NAME", "DEFINITION", "TIME", "VALUE"});
                if (param("DEFINITION", "TABULAR") != "TABULAR" || param("VALUE", "RELATIVE") != "RELATIVE")
                    fail(b.at, "only relative TABULAR amplitude supported");
                auto time = param("TIME", "STEP TIME");
                if (time != "STEP TIME" && time != "TOTAL TIME")
                    fail(b.at, "invalid amplitude TIME");
                IRAmplitude a{required("NAME"), time == "TOTAL TIME", {}, b.at};
                std::vector<double> values;
                for (auto &l : b.data)
                    for (auto &v : fields(l))
                        if (!v.empty())
                            values.push_back(number(v, l.at));
                if (values.empty() || values.size() % 2)
                    fail(b.at, "amplitude requires time/value pairs");
                for (std::size_t i = 0; i < values.size(); i += 2) {
                    if (!a.points.empty() && values[i] <= a.points.back().first)
                        fail(b.at, "amplitude times must increase");
                    a.points.emplace_back(values[i], values[i + 1]);
                }
                m.amplitudes.push_back(std::move(a));
            } else if (b.key == "STEP") {
                params({"NAME", "NLGEOM"});
                if (step || in_part || in_assembly || param("NLGEOM", "YES") != "NO")
                    fail(b.at, "only small-strain sequential steps supported; explicit NLGEOM=NO required");
                m.steps.push_back({});
                step = &m.steps.back();
                step->name = param("NAME", "STEP-" + std::to_string(m.steps.size()));
                step->source = b.at;
                history = false;
                frequency = 1;
                intervals = 0;
                output_context = false;
            } else if (b.key == "END STEP") {
                params({});
                if (!step)
                    fail(b.at, "END STEP without STEP");
                if (step->period <= 0)
                    fail(step->source, "missing DYNAMIC period");
                step = nullptr;
            } else if (b.key == "DYNAMIC") {
                params({"EXPLICIT", "DIRECT USER CONTROL"});
                if (!step || !b.params.contains("EXPLICIT") || step->period)
                    fail(b.at, "one DYNAMIC EXPLICIT required per step");
                auto &l = one();
                auto f = fields(l);
                while (f.size() > 2 && f.back().empty())
                    f.pop_back();
                count(f, 2, 2, l.at);
                step->increment = f[0].empty() ? 0 : number(f[0], l.at);
                step->period = number(f[1], l.at);
                step->direct = b.params.contains("DIRECT USER CONTROL");
                if (step->increment < 0 || step->period <= 0 || (step->direct && step->increment <= 0))
                    fail(l.at, "invalid explicit increment/period");
            } else if (b.key == "BOUNDARY" || b.key == "CLOAD") {
                params({"OP", "AMPLITUDE"});
                if (in_part || in_assembly || (!step && b.key == "CLOAD"))
                    fail(b.at, "invalid load/boundary scope");
                auto op = param("OP", "MOD");
                if (op != "MOD" && op != "NEW")
                    fail(b.at, "OP must be MOD or NEW");
                IRActionBlock ab;
                ab.reset = op == "NEW";
                ab.source = b.at;
                for (auto &l : b.data) {
                    auto f = fields(l);
                    bool bc = b.key == "BOUNDARY";
                    count(f, bc ? 2 : 3, bc ? 4 : 3, l.at);
                    IRAction a;
                    a.target = upper(f[0]);
                    a.amplitude = param("AMPLITUDE");
                    a.source = l.at;
                    auto first = label(f[1], l.at);
                    auto last = bc && f.size() > 2 && !f[2].empty() ? label(f[2], l.at) : first;
                    if (first > 3 || last > 3 || last < first)
                        fail(l.at, "only translation DOF 1..3 supported");
                    a.first = static_cast<std::uint8_t>(first - 1);
                    a.last = static_cast<std::uint8_t>(last - 1);
                    a.value =
                        bc ? (f.size() == 4 && !f[3].empty() ? number(f[3], l.at) : 0) : number(f[2], l.at);
                    if (bc && (a.value != 0 || !a.amplitude.empty()))
                        fail(l.at, "only constant zero boundary supported");
                    ab.actions.push_back(std::move(a));
                }
                if (b.key == "BOUNDARY") {
                    if (step)
                        step->boundaries.push_back(std::move(ab));
                    else
                        m.initial_boundaries.push_back(std::move(ab));
                } else
                    step->loads.push_back(std::move(ab));
            } else if (b.key == "OUTPUT") {
                params({"FIELD", "HISTORY", "FREQUENCY", "NUMBER INTERVAL"});
                if (!step || b.params.contains("FIELD") == b.params.contains("HISTORY"))
                    fail(b.at, "OUTPUT requires FIELD or HISTORY in STEP");
                history = b.params.contains("HISTORY");
                output_pending = true;
                output_context = true;
                frequency = 1;
                intervals = 0;
                if (b.params.contains("FREQUENCY")) {
                    auto v = param("FREQUENCY");
                    frequency = v == "0" ? 0 : label(v, b.at);
                }
                if (b.params.contains("NUMBER INTERVAL")) {
                    if (b.params.contains("FREQUENCY"))
                        fail(b.at, "choose output frequency or intervals");
                    intervals = label(param("NUMBER INTERVAL"), b.at);
                    if (intervals > 100000000)
                        fail(b.at, "output interval count exceeds budget");
                }
                if (!b.data.empty())
                    fail(b.at, "OUTPUT has no data lines");
            } else if (b.key == "NODE OUTPUT" || b.key == "ELEMENT OUTPUT" || b.key == "ENERGY OUTPUT") {
                if (b.key == "NODE OUTPUT")
                    params({"NSET"});
                else if (b.key == "ELEMENT OUTPUT")
                    params({"ELSET", "POSITION"});
                else
                    params({});
                if (!step || !output_context)
                    fail(b.at, "output requires STEP and OUTPUT FIELD/HISTORY context");
                output_pending = false;
                if (param("POSITION", "INTEGRATION POINTS") != "INTEGRATION POINTS")
                    fail(b.at, "only IP element output supported");
                IROutput o{history, frequency, intervals, param(b.key == "NODE OUTPUT" ? "NSET" : "ELSET"),
                           {},      b.at};
                for (auto &l : b.data)
                    for (auto &v : fields(l)) {
                        auto name = upper(v);
                        if (name.empty())
                            continue;
                        bool ok = b.key == "NODE OUTPUT"
                                      ? (name == "U" || name == "V" || name == "A" || name == "RF")
                                      : (b.key == "ELEMENT OUTPUT" ? (name == "S" || name == "E")
                                                                   : (name == "ALLKE" || name == "ALLSE"));
                        if (!ok)
                            fail(l.at, "unsupported output variable: " + name);
                        o.variables.push_back(name);
                    }
                if (o.variables.empty())
                    fail(b.at, "empty output variable list");
                step->outputs.push_back(std::move(o));
            } else
                unsupported(b, "unsupported physical keyword *" + b.key);
            session.entities += b.data.size();
            if (session.entities > options.max_entities)
                fail(b.at, "input entity/record budget exceeded");
        }
        if (in_part || in_assembly || in_instance || step)
            fail({path.string(), 1, 1}, "unclosed PART/ASSEMBLY/INSTANCE/STEP");
        if(pending_coupling) fail(m.extensions.couplings.at(*pending_coupling).source,"COUPLING missing KINEMATIC");
        auto qualify=[&](SourceKey &key,const Source &at) {
            if(key.instance.empty()) return;
            auto i=std::find_if(m.instances.begin(),m.instances.end(),[&](auto &v){return v.name==key.instance;});
            if(i==m.instances.end()) fail(at,"unknown source node instance");
            key.part=i->part;
        };
        for(auto &e:m.foundation.equations) for(auto &t:e.terms) qualify(t.node,e.source);
        for(auto &c:m.extensions.couplings) qualify(c.reference,c.source);
        for(auto &i:m.foundation.initial_conditions) qualify(i.node,i.source);
        result.report.parsed = true;
        return result;
    }
};
class ReaderRegistry {
    std::vector<std::unique_ptr<IModelReader>> readers_;

  public:
    ReaderRegistry() { add(std::make_unique<AbaqusInpReader>()); }
    void add(std::unique_ptr<IModelReader> r) {
        if (!r)
            throw std::invalid_argument("null reader");
        readers_.push_back(std::move(r));
    }
    ReadResult read(const std::filesystem::path &path, ReaderOptions options = {}) const {
        std::ifstream in(path, std::ios::binary);
        if (!in)
            fail({path.string(), 1, 1}, "cannot open model");
        char prefix[4096];
        in.read(prefix, sizeof(prefix));
        std::string sample(prefix, static_cast<std::size_t>(in.gcount()));
        in.clear();
        in.seekg(0);
        const IModelReader *selected = nullptr;
        for (auto &r : readers_)
            if (r->probe(path.extension().string(), sample)) {
                if (selected)
                    fail({path.string(), 1, 1}, "ambiguous input reader");
                selected = r.get();
            }
        if (!selected)
            fail({path.string(), 1, 1}, "no registered reader");
        return selected->read(in, path, std::move(options));
    }
};
} // namespace sdb::lifecycle
