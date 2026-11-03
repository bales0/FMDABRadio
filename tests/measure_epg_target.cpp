// Compile-only layout probe: never part of the radio firmware or its heap.
#include "../src/epg_core.h"
#include "../src/UiTextCache.h"
extern "C" {
extern const uint32_t epg_target_layout[] = {
  sizeof(epg::Core), 3*epg::kObjectBytes+epg::kDirectoryBytes+epg::kTextBytes,
  sizeof(epg::Core)+3*epg::kObjectBytes+epg::kDirectoryBytes+epg::kTextBytes,
  sizeof(ui_paint::ListeningCache),
  2*epg::kObjectBytes+epg::kDirectoryBytes+epg::kTextBytes,
  sizeof(epg::Core)+2*epg::kObjectBytes+epg::kDirectoryBytes+epg::kTextBytes
};
}
static_assert(sizeof(epg::Core)+3*epg::kObjectBytes+epg::kDirectoryBytes+epg::kTextBytes<=65536,
              "Re-audit EPG RAM before increasing the 64KiB headless budget");
