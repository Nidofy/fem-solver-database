#include <sdb/coverage/audit.hpp>
#include <iomanip>
#include <iostream>

template<class F> static void rejects(F action) {
    try { action(); } catch (const std::invalid_argument &) { return; }
    throw std::runtime_error("invalid coverage inventory accepted");
}

int main(int argc, char **argv) {
    try {
        if (argc != 2) return 2;
        std::ifstream in(argv[1]);
        sdb::DataCoverage contract;
        contract.consumer = "DBF13 offline audit";
        if (!(in >> std::quoted(contract.source_revision))) return 1;
        std::vector<std::string> declared;
        sdb::DataResponsibility row;
        while (in >> std::quoted(row.source)) {
            unsigned disposition = 0;
            if (!(in >> std::quoted(row.target) >> std::quoted(row.semantics) >>
                  std::quoted(row.evidence) >> disposition) || disposition > 3) return 1;
            row.owner = "Track S";
            row.disposition = static_cast<sdb::DataDisposition>(disposition);
            declared.push_back(row.source);
            contract.entries.push_back(row);
        }
        if (!in.eof() || declared.size() != 2047) return 1;
        const auto inventory = sdb::coverage::audit(contract, declared, declared);
        if (inventory.unsupported_populated.size() != inventory.declared_by_disposition[3]) return 1;
        const auto empty = sdb::coverage::audit(contract, declared, {});
        if (!empty.admits_capture()) return 1;

        // Verify the admission distinction independently of the state of native qualification.
        contract.entries[0].disposition = sdb::DataDisposition::Stored;
        contract.entries[1].disposition = sdb::DataDisposition::Unsupported;
        std::vector<std::string> capture{declared[0]};
        if (!sdb::coverage::audit(contract, declared, capture).admits_capture()) return 1;
        capture.push_back(declared[1]);
        if (sdb::coverage::audit(contract, declared, capture).unsupported_populated !=
            std::vector<std::string>{declared[1]}) return 1;
        capture.push_back(declared[0]);
        rejects([&]{sdb::coverage::audit(contract, declared, capture);});
        capture = {"missing:1"};
        rejects([&]{sdb::coverage::audit(contract, declared, capture);});
        declared.pop_back();
        rejects([&]{sdb::coverage::audit(contract, declared, {});});
        std::cout << "audited=2047 unsupported_declarations=" << inventory.declared_by_disposition[3] << '\n';
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
