// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
namespace launcher {
struct Settings {
    bool autoRefresh = true;
    bool alphabetical = true;
    void load();
    bool save() const;
};
}
