#pragma once
#include "results.hpp"
#include <iomanip>

namespace sdb::lifecycle {
inline std::string xml(std::string s) {
    std::string out;
    for (char c : s) {
        switch (c) {
        case '&':
            out += "&amp;";
            break;
        case '<':
            out += "&lt;";
            break;
        case '>':
            out += "&gt;";
            break;
        case '"':
            out += "&quot;";
            break;
        case '\'':
            out += "&apos;";
            break;
        default:
            out += c;
        }
    }
    return out;
}
inline std::vector<double> dense(const ResultField &f, std::size_t count) {
    auto width = f.descriptor.components.size();
    std::vector<double> out(product(count, width), std::numeric_limits<double>::quiet_NaN());
    for (std::size_t i = 0; i < f.ids.size(); ++i) {
        auto id = f.descriptor.location == Location::Global ? 0 : f.ids[i] - 1;
        if (id >= count)
            throw std::runtime_error("export entity out of bounds");
        std::copy_n(f.values.begin() + i * width, width, out.begin() + id * width);
    }
    return out;
}
inline std::string field_name(const ResultDescriptor &d) {
    return d.name + (d.region.empty() ? "" : "@" + d.region);
}
class ResultExporter {
  public:
    static void vtk(const ResultReader &reader, const std::filesystem::path &directory) {
        std::filesystem::create_directories(directory);
        std::ofstream pvd(directory / "series.pvd");
        pvd << std::setprecision(17)
            << "<?xml version=\"1.0\"?><VTKFile type=\"Collection\" version=\"0.1\" "
               "byte_order=\"LittleEndian\"><Collection>\n";
        for (std::size_t index = 0; index < reader.index.size(); ++index) {
            auto frame = reader.frame(index);
            auto filename = "frame_" + std::to_string(frame.info.id) + ".vtu";
            std::ofstream out(directory / filename);
            out << std::setprecision(17)
                << "<?xml version=\"1.0\"?><VTKFile type=\"UnstructuredGrid\" version=\"0.1\" "
                   "byte_order=\"LittleEndian\"><UnstructuredGrid><Piece NumberOfPoints=\""
                << reader.model.nodes.size() << "\" NumberOfCells=\"" << reader.model.elements.size()
                << "\">\n<Points><DataArray type=\"Float64\" NumberOfComponents=\"3\" format=\"ascii\">";
            for (auto &x : reader.model.coordinates)
                for (auto v : x)
                    out << v << ' ';
            out << "</DataArray></Points><Cells><DataArray type=\"Int64\" Name=\"connectivity\" "
                   "format=\"ascii\">";
            for (auto &ns : reader.model.connectivity)
                for (auto n : ns)
                    out << n << ' ';
            out << "</DataArray><DataArray type=\"Int64\" Name=\"offsets\" format=\"ascii\">";
            std::size_t offset = 0;
            for (auto &ns : reader.model.connectivity) {
                offset += ns.size();
                out << offset << ' ';
            }
            out << "</DataArray><DataArray type=\"UInt8\" Name=\"types\" format=\"ascii\">";
            for (auto &d : reader.model.descriptors)
                out << (d.shape == Topology::Tet4
                            ? 10
                            : (d.shape == Topology::Hex8 ? 12 : (d.shape == Topology::Quad4 ? 9 : 3)))
                    << ' ';
            out << "</DataArray></Cells>\n";
            for (bool node : {true, false}) {
                out << (node ? "<PointData>" : "<CellData>");
                for (auto &f : frame.fields)
                    if (!f.descriptor.history &&
                        (node ? f.descriptor.location == Location::Node
                              : f.descriptor.location == Location::Element ||
                                    f.descriptor.location == Location::IntegrationPoint)) {
                        out << "<DataArray type=\"Float64\" Name=\"" << xml(field_name(f.descriptor))
                            << "\" NumberOfComponents=\"" << f.descriptor.components.size()
                            << "\" format=\"ascii\">";
                        for (auto v :
                             dense(f, node ? reader.model.nodes.size() : reader.model.elements.size()))
                            out << v << ' ';
                        out << "</DataArray>";
                    }
                out << (node ? "</PointData>" : "</CellData>");
            }
            out << "</Piece></UnstructuredGrid></VTKFile>\n";
            out.close();
            if (!out)
                throw std::runtime_error("VTU write failed");
            pvd << "<DataSet timestep=\"" << frame.info.time << "\" file=\"" << filename << "\"/>\n";
        }
        pvd << "</Collection></VTKFile>\n";
        pvd.close();
        if (!pvd)
            throw std::runtime_error("PVD write failed");
    }
    // Dependency-free XDMF XML DataItems. No HDF5 URI or .h5 files are fabricated.
    static void xdmf(const ResultReader &reader, const std::filesystem::path &path) {
        std::ofstream out(path);
        out << std::setprecision(17)
            << "<?xml version=\"1.0\"?><Xdmf Version=\"3.0\"><Domain><Grid Name=\"Results\" "
               "GridType=\"Collection\" CollectionType=\"Temporal\">\n";
        std::vector<Index> mixed;
        for (std::size_t i = 0; i < reader.model.connectivity.size(); ++i) {
            auto &d = reader.model.descriptors[i];
            mixed.push_back(d.shape == Topology::Tet4
                                ? 6
                                : (d.shape == Topology::Hex8 ? 9 : (d.shape == Topology::Quad4 ? 5 : 2)));
            if (d.shape == Topology::Beam2)
                mixed.push_back(2);
            auto &ns = reader.model.connectivity[i];
            mixed.insert(mixed.end(), ns.begin(), ns.end());
        }
        for (std::size_t i = 0; i < reader.index.size(); ++i) {
            auto frame = reader.frame(i);
            out << "<Grid Name=\"Frame" << frame.info.id << "\"><Time Value=\"" << frame.info.time
                << "\"/><Topology TopologyType=\"Mixed\" NumberOfElements=\"" << reader.model.elements.size()
                << "\"><DataItem Format=\"XML\" NumberType=\"UInt\" Dimensions=\"" << mixed.size() << "\">";
            for (auto v : mixed)
                out << v << ' ';
            out << "</DataItem></Topology><Geometry GeometryType=\"XYZ\"><DataItem Format=\"XML\" "
                   "NumberType=\"Float\" Precision=\"8\" Dimensions=\""
                << reader.model.nodes.size() << " 3\">";
            for (auto &x : reader.model.coordinates)
                for (auto v : x)
                    out << v << ' ';
            out << "</DataItem></Geometry>";
            for (auto &f : frame.fields)
                if (!f.descriptor.history) {
                    auto &d = f.descriptor;
                    bool global = d.location == Location::Global, node = d.location == Location::Node;
                    auto count =
                        global ? 1 : (node ? reader.model.nodes.size() : reader.model.elements.size());
                    auto width = d.components.size();
                    out << "<Attribute Name=\"" << xml(field_name(d)) << "\" Center=\""
                        << (global ? "Grid" : (node ? "Node" : "Cell")) << "\" AttributeType=\""
                        << (width == 1 ? "Scalar" : (width == 3 ? "Vector" : "Tensor6"))
                        << "\"><Information Name=\"TimeLevel\" Value=\"current\"/><Information "
                           "Name=\"Provenance\" Value=\""
                        << xml(d.provenance)
                        << "\"/><DataItem Format=\"XML\" NumberType=\"Float\" Precision=\"8\" Dimensions=\""
                        << count << ' ' << width << "\">";
                    for (auto v : dense(f, count))
                        out << v << ' ';
                    out << "</DataItem></Attribute>";
                }
            out << "</Grid>\n";
        }
        out << "</Grid></Domain></Xdmf>\n";
        out.close();
        if (!out)
            throw std::runtime_error("XDMF write failed");
    }
};
} // namespace sdb::lifecycle
