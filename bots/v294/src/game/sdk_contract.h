#pragma once
// Engine 2.0.0 writes 84 u32 counts to champion_items. An old SDK allocates
// only 42: reject it at build time rather than risk a 168-byte overwrite.
static_assert(static_cast<unsigned>(cambc::item_type::count) == 84,
              "Patch 2.0.0 requires the CLI 4.0.0 SDK (84 item types). Refresh the SDK before building.");
