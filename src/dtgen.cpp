/*
 * Copyright (c) 2020 National Instruments
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 */

#include "dtgen.h"

namespace dtgen {

static std::string indent(int indent)
{
    std::string s;
    while (indent--)
        s += "\t";
    return s;
}

static std::string hexify(unsigned long long val)
{
    char tmp[128];

    if (val < 16)
        sprintf(tmp, "%llx", val);
    else if (val < 0xFF)
        sprintf(tmp, "%02llx", val);
    else if (val < 0xFFFF)
        sprintf(tmp, "%04llx", val);
    else if (val <= 0xFFFFFFFF)
        sprintf(tmp, "%08llx", val);
    else if (val <= 0xFFFFFFFFFF)
        sprintf(tmp, "%010llx", val);
    else
        sprintf(tmp, "%016llx", val);

    return std::string(tmp);
}

dt_node::dt_node(
    std::string name, std::optional<unsigned long long> unit, std::string label)
    : name(name), unit(unit), label(label)
{
}

void dt_node::add_property(std::string prop)
{
    add_property_(prop + ";");
}
void dt_node::add_property(std::string prop, const char* value)
{
    add_property_(prop + " = \"" + std::string(value) + "\";");
}

void dt_node::add_property(std::string prop, uint32_t value)
{
    add_property_(prop + " = <0x" + hexify(value) + ">;");
}

void dt_node::add_property(std::string prop, const std::vector<uint32_t>& values)
{
    std::string s;

    s += prop + " = <";
    for (uint32_t value : values)
        s += std::string("0x") + hexify(value) + " ";

    s += ">;";

    add_property_(s);
}

void dt_node::add_property_phandle(std::string prop, std::string value)
{
    add_property_(prop + " = <&" + value + ">;");
}

void dt_node::add_property_(std::string prop)
{
    properties.push_back(prop);
}

void dt_node::add_node(std::unique_ptr<dt_node> node)
{
    child_nodes.push_back(std::move(node));
}

std::string dt_node::render(int depth) const
{
    std::string buf = indent(depth);

    if (!label.empty()) {
        buf += label;
        buf += ": ";
    }

    buf += name;

    if (unit) {
        buf += "@";
        buf += hexify(*unit);
    }
    buf += " {\n";

    for (auto&& prop : properties)
        buf += indent(depth + 1) + prop + "\n";

    for (auto&& node : child_nodes)
        buf += node->render(depth + 1);

    buf += indent(depth);
    buf += "};\n";

    return buf;
}

} // namespace dtgen
