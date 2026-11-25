// SPDX-License-Identifier: Apache-2.0

// The plugin's CLAP entry, which clap-wrapper's VST3 and AUv2 load as well.

#include <anira/abi/core.h>
#include <anira/abi/status.h>
#include <anira/abi/version.h>
#include <clap/clap.h>

#include <cstdint>
#include <cstring>

#include "Plugin.h"

namespace {

using anira::examples::clap_plugin::Plugin;

std::uint32_t plugin_count(const clap_plugin_factory* /*factory*/) {
    return 1;
}

const clap_plugin_descriptor* plugin_descriptor(const clap_plugin_factory* /*factory*/,
                                                std::uint32_t index) {
    return index == 0 ? Plugin::descriptor() : nullptr;
}

const clap_plugin* create_plugin(const clap_plugin_factory* /*factory*/,
                                 const clap_host* host,
                                 const char* plugin_id) {
    if (std::strcmp(plugin_id, Plugin::descriptor()->id) != 0) { return nullptr; }
    auto* plugin = new Plugin(host);  // owned by the host: destroy() deletes it
    return plugin->clapPlugin();
}

const clap_plugin_factory k_factory{.get_plugin_count = plugin_count,
                                    .get_plugin_descriptor = plugin_descriptor,
                                    .create_plugin = create_plugin};

const void* get_factory(const char* factory_id) {
    return std::strcmp(factory_id, CLAP_PLUGIN_FACTORY_ID) == 0 ? &k_factory : nullptr;
}

bool init(const char* /*path*/) {
    // The anira this plugin loads must be the ABI its headers describe.
    return anira_check_abi(ANIRA_ABI_VERSION) == ANIRA_OK;
}

void deinit() {
    // Before the host unloads the library: anira's backstop for a static embedding (idempotent;
    // refuses while a context or handler of this copy still exists).
    anira_shutdown();
}

}  // namespace

extern "C" {
// NOLINTNEXTLINE(readability-identifier-naming): the name CLAP looks up
CLAP_EXPORT const clap_plugin_entry clap_entry{.clap_version = CLAP_VERSION_INIT,
                                               .init = init,
                                               .deinit = deinit,
                                               .get_factory = get_factory};
}
