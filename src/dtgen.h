#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace dtgen {

struct dt_node
{
    dt_node(std::string name,
        std::optional<unsigned long long> unit = std::nullopt,
        std::string label                      = "");

    void add_property(std::string prop);
    void add_property(std::string prop, const char* value);
    void add_property(std::string prop, uint32_t value);
    void add_property(std::string prop, const std::vector<uint32_t>& values);
    void add_property_phandle(std::string prop, std::string value);

    void add_node(std::unique_ptr<dt_node> node);
    std::string render(int depth) const;

private:
    void add_property_(std::string);

    const std::string name;
    const std::optional<unsigned long long> unit;
    const std::string label;
    std::vector<std::string> properties;
    std::vector<std::unique_ptr<dt_node>> child_nodes;
};

} // namespace dtgen
