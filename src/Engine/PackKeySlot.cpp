// The key slot the exporter patches in a copied runtime executable (see
// AssetFS.cpp). It must stay the only copy of these bytes in the runtime:
// the exporter finds the slot by its marker, the first 16 bytes, so nothing
// else may hold them (AssetFS reads the marker from here, it has no copy).

#include <cstdint>

extern "C" {
    // volatile: every read goes to the bytes in the file, which the exporter
    // changes after the build; the compiler mustn't assume their values.
    extern volatile const std::uint8_t tilkyPackKeySlot[64];

    alignas(16) volatile const std::uint8_t tilkyPackKeySlot[64] = {
        // Marker (overwritten with random noise in an exported runtime).
        0x7F, 'T', 'i', 'l', 'k', 'y', 'P', 'a', 'k', 'S', 'l', 'o', 't', 0x01, 0x9E, 0x37,
        // Masked key (32 bytes), check (4 bytes), spare (12 bytes): zero until patched.
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    };
}
