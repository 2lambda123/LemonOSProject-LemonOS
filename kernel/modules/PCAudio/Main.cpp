#include "Audio.h"

#include "AC97.h"
#include "HDAudio.h"

#include <Module.h>

#include <Memory.h>
#include <PCI.h>
#include <PCIVendors.h>
#include <Pair.h>
#include <Thread.h>

namespace Audio {
static const Pair<uint16_t, uint16_t> controllerPCIIDs[] = {
    {PCI::VendorAMD, 0x1457},   {PCI::VendorAMD, 0x1487},   {PCI::VendorAMD, 0x15e3},   {PCI::VendorIntel, 0x1c20},
    {PCI::VendorIntel, 0x1d20}, {PCI::VendorIntel, 0x8c20}, {PCI::VendorIntel, 0x8ca0}, {PCI::VendorIntel, 0x8d20},
    {PCI::VendorIntel, 0x8d21}, {PCI::VendorIntel, 0xa1f0}, {PCI::VendorIntel, 0xa270}, {PCI::VendorIntel, 0x9c20},
    {PCI::VendorIntel, 0x9c21}, {PCI::VendorIntel, 0x9ca0}, {PCI::VendorIntel, 0xa170}, {PCI::VendorIntel, 0x9d70},
    {PCI::VendorIntel, 0xa171}, {PCI::VendorIntel, 0x9d71}, {PCI::VendorIntel, 0xa2f0}, {PCI::VendorIntel, 0xa348},
    {PCI::VendorIntel, 0x9dc8}, {PCI::VendorIntel, 0x02c8}, {PCI::VendorIntel, 0x06c8}, {PCI::VendorIntel, 0xf1c8},
    {PCI::VendorIntel, 0xa3f0}, {PCI::VendorIntel, 0xf0c8}, {PCI::VendorIntel, 0x34c8}, {PCI::VendorIntel, 0x3dc8},
    {PCI::VendorIntel, 0x38c8}, {PCI::VendorIntel, 0x4dc8}, {PCI::VendorIntel, 0xa0c8}, {PCI::VendorIntel, 0x43c8},
    {PCI::VendorIntel, 0x490d}, {PCI::VendorIntel, 0x7ad0}, {PCI::VendorIntel, 0x51c8}, {PCI::VendorIntel, 0x4b55},
    {PCI::VendorIntel, 0x4b58}, {PCI::VendorIntel, 0x5a98}, {PCI::VendorIntel, 0x1a98}, {PCI::VendorIntel, 0x3198},
    {PCI::VendorIntel, 0x0a0c}, {PCI::VendorIntel, 0x0c0c}, {PCI::VendorIntel, 0x0d0c}, {PCI::VendorIntel, 0x160c},
    {PCI::VendorIntel, 0x3b56}, {PCI::VendorIntel, 0x811b}, {PCI::VendorIntel, 0x080a}, {PCI::VendorIntel, 0x0f04},
    {PCI::VendorIntel, 0x2284}, {PCI::VendorIntel, 0x2668}, {PCI::VendorIntel, 0x27d8}, {PCI::VendorIntel, 0x269a},
    {PCI::VendorIntel, 0x284b}, {PCI::VendorIntel, 0x293e}, {PCI::VendorIntel, 0x293f}, {PCI::VendorIntel, 0x3a3e},
    {PCI::VendorIntel, 0x3a6e},
};

static Vector<AudioController*>* audioControllers;

int ModuleInit() {
    audioControllers = new Vector<AudioController*>();

    for (auto& p : controllerPCIIDs) {
        PCI::EnumeratePCIDevices(p.item2, p.item1, [](const PCIInfo& info) -> void {
            IntelHDAudioController* cnt = new IntelHDAudioController(info);

            DeviceManager::RegisterDevice(cnt);
            audioControllers->add_back(cnt);
        });
    }

    PCI::EnumerateGenericPCIDevices(PCI_CLASS_MULTIMEDIA, PCI_SUBCLASS_AC97, [](const PCIInfo& info) -> void {
        AC97Controller* cnt = new AC97Controller(info);

        DeviceManager::RegisterDevice(cnt);
        audioControllers->add_back(cnt);
    });

    Log::Info("[PCAudio] Found controllers: %d", audioControllers->get_length());

    if (audioControllers->get_length() <= 0) {
        return 1; // No controllers present, module can be unloaded
    }

    return 0;
}

int ModuleExit() {
    for (auto& cnt : *audioControllers) {
        delete cnt;
    }
    audioControllers->clear();

    delete audioControllers;
    return 0;
}

DECLARE_MODULE("pcaudio", "AC97 and Intel HD Audio Controller Driver", ModuleInit, ModuleExit);
} // namespace Audio
