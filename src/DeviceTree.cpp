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

#include "DeviceTree.h"
#include "dtgen.h"

namespace nirio {

static uint64_t base_address(const nirio::Bitfile& bitfile)
{
    (void)bitfile;
    // TODO: Make dependent upon target class
    return 0x1300000000ULL;
}

static uint32_t upper(uint64_t v)
{
    return static_cast<uint32_t>(v >> 32);
}

static uint32_t lower(uint64_t v)
{
    return static_cast<uint32_t>(v & 0xFFFFFFFF);
}

// unpack a 0 padded hex string into words
static std::vector<uint32_t> unhexify(const std::string& s)
{
    std::vector<uint32_t> v;
    char buf[9];

    assert(s.size() % 8 == 0);
    for (size_t i = 0; i < s.size() / 8; i++) {
        s.copy(buf, 8, i * 8);
        buf[8] = 0;
        v.push_back(strtoul(buf, NULL, 16));
    }

    return v;
}

static auto gen_rio_node(const nirio::Bitfile& bitfile)
{
    using dtgen::dt_node;

    const auto base          = base_address(bitfile);
    const auto size          = 0x80000ULL;
    const auto fifo_base     = base + 0x40000;
    const auto fifo_reg_size = 0x40;
    auto&& fifos             = bitfile.getFifos();

    auto rio = std::make_unique<dt_node>("nirio", base);
    rio->add_property("#address-cells", 1);
    rio->add_property("#size-cells", 1);
    rio->add_property("compatible", "ni,rio");
    rio->add_property("status", "okay");

    rio->add_property("signature", unhexify(bitfile.getSignature()));
    rio->add_property("control-offset", bitfile.getControlRegister());
    rio->add_property("signature-offset", bitfile.getSignatureRegister());
    rio->add_property("reset-offset", bitfile.getResetRegister());
    rio->add_property("irq-enable-offset", bitfile.getIrqEnableRegister());
    rio->add_property("irq-mask-offset", bitfile.getIrqMaskRegister());
    rio->add_property("irq-status-offset", bitfile.getIrqStatusRegister());

    rio->add_property("reg", {upper(base), lower(base), upper(size), lower(size)});

    if (bitfile.isResetAutoClears())
        rio->add_property("ni,reset-auto-clears");

    if (bitfile.isAutoRunWhenDownloaded())
        rio->add_property("ni,run-when-loaded");

    rio->add_property("dma-coherent");

    rio->add_property_phandle("interrupt-parent", "gic");
    rio->add_property("interrupts", {0, 89, 4});

    // TODO: Reconsider this. The InChWORM IO space is located at 0x40000,
    // which really means that all LVFPGA registers are referenced to this
    // address.  Perhaps the top-level device tree entry should be the
    // InChWORM, with a ranges property for the IO space. Under that, have the
    // nodes for the LVFPGA stuff.

    uint32_t min_offset = 0xFFFFFFFF;
    for (auto&& fifo : fifos)
        min_offset = std::min(min_offset, fifo.getOffset());

    rio->add_property("ranges",
        {0x0,
            upper(fifo_base + min_offset),
            lower(fifo_base + min_offset),
            static_cast<uint32_t>(fifo_reg_size * fifos.size())});

    for (auto&& fifo : fifos) {
        auto node = std::make_unique<dt_node>("dma-fifo", fifo.getNumber());

        node->add_property("compatible", "ni,rio-fifo");
        node->add_property("label", fifo.getName().c_str());
        node->add_property("dma-channel", fifo.getNumber());
        node->add_property("bits-per-element", fifo.getType().getElementBytes() * 8);

        node->add_property("reg", {fifo.getOffset() - min_offset, fifo_reg_size});

        if (fifo.isTargetToHost())
            node->add_property("ni,target-to-host");
        else
            node->add_property("ni,host-to-target");

        rio->add_node(std::move(node));
    }

    return rio;
}

// TODO: Remove eventually, once most bitfiles are built using new style overlays
std::string generateOldOverlay(const nirio::Bitfile& bitfile)
{
    using dtgen::dt_node;

    auto overlay = std::make_unique<dt_node>("__overlay__");
    overlay->add_property("#address-cells", 2);
    overlay->add_property("#size-cells", 2);
    overlay->add_node(gen_rio_node(bitfile));

    auto fragment = std::make_unique<dt_node>("fragment", 100);
    fragment->add_property_phandle("target", "amba");
    fragment->add_node(std::move(overlay));

    auto dtso = bitfile.getOverlay();
    dtso.insert(dtso.rfind("};"), fragment->render(0));

    return dtso;
}

std::string generateOverlay(const nirio::Bitfile& bitfile)
{
    using dtgen::dt_node;

    auto overlay = std::make_unique<dt_node>("&fpga_full");
    overlay->add_node(gen_rio_node(bitfile));

    auto dtso = bitfile.getOverlay();
    dtso.insert(dtso.size(), overlay->render(0));

    return dtso;
}

std::string generateDeviceTree(const nirio::Bitfile& bitfile)
{
    auto overlay = bitfile.getOverlay();

    if (overlay.find("__overlay__") != -1)
        return generateOldOverlay(bitfile);
    else
        return generateOverlay(bitfile);
}

} // namespace nirio
