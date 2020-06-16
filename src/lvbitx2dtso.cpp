#include "Bitfile.h"
#include "dtgen.h"
#include <stdio.h>
#include <unistd.h>
#include <iostream>
#include <memory>

using dtgen::dt_node;

static uint64_t base_address(const nirio::Bitfile& bitfile)
{
    (void)bitfile;
    // TODO: Make dependent upon target class
    return 0x1200000000ULL;
}

static uint32_t upper(uint64_t v)
{
    return static_cast<uint32_t>(v >> 32);
}

static uint32_t lower(uint64_t v)
{
    return static_cast<uint32_t>(v & 0xFFFFFFFF);
}

static auto gen_rio_node(const nirio::Bitfile& bitfile)
{
    const auto base          = base_address(bitfile);
    const auto size          = 0x60000ULL;
    const auto fifo_base     = base + 0x2000;
    const auto fifo_reg_size = 0x40;
    auto&& fifos             = bitfile.getFifos();

    auto rio = std::make_unique<dt_node>("nirio", base);
    rio->add_property("#address-cells", 1);
    rio->add_property("#size-cells", 1);
    rio->add_property("compatible", "ni,rio");
    rio->add_property("status", "okay");

    rio->add_property("control-offset", bitfile.getControlRegister());
    rio->add_property("signature-offset", bitfile.getSignatureRegister());
    rio->add_property("reset-offset", bitfile.getResetRegister());

    rio->add_property("reg", {upper(base), lower(base), upper(size), lower(size)});
    rio->add_property("ranges",
        {0x0,
            upper(fifo_base),
            lower(fifo_base),
            static_cast<uint32_t>(fifo_reg_size * fifos.size())});

    if (bitfile.isResetAutoClears())
        rio->add_property("ni,reset-auto-clears");

    if (bitfile.isAutoRunWhenDownloaded())
        rio->add_property("ni,run-when-loaded");

    uint32_t min_offset = 0xFFFFFFFF;
    for (auto&& fifo : fifos)
        min_offset = std::min(min_offset, fifo.getOffset());

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

    auto overlay = std::make_unique<dt_node>("__overlay__");
    overlay->add_property("#address-cells", 2);
    overlay->add_property("#size-cells", 2);
    overlay->add_node(std::move(rio));

    auto fragment = std::make_unique<dt_node>("fragment", 100);
    fragment->add_property_phandle("target", "amba");
    fragment->add_node(std::move(overlay));

    return fragment;
}

static std::string generateDeviceTree(const nirio::Bitfile& bitfile)
{
    auto overlay = bitfile.getOverlay();
    overlay.insert(overlay.rfind("};"), gen_rio_node(bitfile)->render(0));
    return overlay;
}

int main(int argc, char** argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s <bitfile.lvbitx>\n", argv[0]);
        return 1;
    }

    nirio::Bitfile bitfile(argv[1]);

    std::cout << generateDeviceTree(bitfile) << std::endl;

    return 0;
}
