/*
 * Copyright (C) 2022 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <string>

#include <android-base/logging.h>
#include <cutils/properties.h>

#include "wifi_feature_flags.h"

namespace aidl {
namespace android {
namespace hardware {
namespace wifi {
namespace feature_flags {

/* The chip may either have a single mode supporting any number of combinations,
 * or a fixed dual-mode (so it involves firmware loading to switch between
 * modes) setting. If there is a need to support more modes, it needs to be
 * implemented manually in WiFi HAL (see changeFirmwareMode in
 * WifiChip::handleChipConfiguration).
 *
 * Supported combinations are defined in device's makefile, for example:
 *    WIFI_HAL_INTERFACE_COMBINATIONS := {{{STA, AP}, 1}, {{P2P, NAN}, 1}},
 *    WIFI_HAL_INTERFACE_COMBINATIONS += {{{STA}, 1}, {{AP}, 2}}
 * What this means:
 *    Interface concurrency combination 1: 1 STA or AP and 1 P2P or NAN concurrent iface
 *                             operations.
 *    Interface concurrency combination 2: 1 STA and 2 AP concurrent iface operations.
 *
 * For backward compatibility, the following makefile flags can be used to
 * generate combinations list:
 *  - WIFI_HIDL_FEATURE_DUAL_INTERFACE
 *  - WIFI_HIDL_FEATURE_DISABLE_AP
 *  - WIFI_HIDL_FEATURE_AWARE
 * However, they are ignored if WIFI_HAL_INTERFACE_COMBINATIONS was provided.
 * With WIFI_HIDL_FEATURE_DUAL_INTERFACE flag set, there is a single mode with
 * two concurrency combinations:
 *    Interface Concurrency Combination 1: Will support 1 STA and 1 P2P or NAN (optional)
 *                             concurrent iface operations.
 *    Interface Concurrency Combination 2: Will support 1 STA and 1 AP concurrent
 *                             iface operations.
 *
 * The only dual-mode configuration supported is for alternating STA and AP
 * mode, that may involve firmware reloading. In such case, there are 2 separate
 * modes of operation with 1 concurrency combination each:
 *    Mode 1 (STA mode): Will support 1 STA and 1 P2P or NAN (optional)
 *                       concurrent iface operations.
 *    Mode 2 (AP mode): Will support 1 AP iface operation.
 *
 * If Aware is enabled, the concurrency combination will be modified to support either
 * P2P or NAN in place of just P2P.
 */
// clang-format off
#ifdef WIFI_HAL_INTERFACE_COMBINATIONS
constexpr int kMainModeId = chip_mode_ids::kV3;
#elif defined(WIFI_HIDL_FEATURE_DUAL_INTERFACE)
// former V2 (fixed dual interface) setup expressed as V3
constexpr int kMainModeId = chip_mode_ids::kV3;
#  ifdef WIFI_HIDL_FEATURE_DISABLE_AP
#    ifdef WIFI_HIDL_FEATURE_AWARE
//     1 STA + 1 of (P2P or NAN)
#      define WIFI_HAL_INTERFACE_COMBINATIONS {{{STA}, 1}, {{P2P, NAN}, 1}}
#    else
//     1 STA + 1 P2P
#      define WIFI_HAL_INTERFACE_COMBINATIONS {{{STA}, 1}, {{P2P}, 1}}
#    endif
#  else
#    ifdef WIFI_HIDL_FEATURE_AWARE
//     (1 STA + 1 AP) or (1 STA + 1 of (P2P or NAN))
#      define WIFI_HAL_INTERFACE_COMBINATIONS {{{STA}, 1}, {{AP}, 1}},\
                                              {{{STA}, 1}, {{P2P, NAN}, 1}}
#    else
//     (1 STA + 1 AP) or (1 STA + 1 P2P)
#      define WIFI_HAL_INTERFACE_COMBINATIONS {{{STA}, 1}, {{AP}, 1}},\
                                              {{{STA}, 1}, {{P2P}, 1}}
#    endif
#  endif
#else
// V1 (fixed single interface, dual-mode chip)
constexpr int kMainModeId = chip_mode_ids::kV1Sta;
#  ifdef WIFI_HIDL_FEATURE_AWARE
//   1 STA + 1 of (P2P or NAN)
#    define WIFI_HAL_INTERFACE_COMBINATIONS {{{STA}, 1}, {{P2P, NAN}, 1}}
#  else
//   1 STA + 1 P2P
#    define WIFI_HAL_INTERFACE_COMBINATIONS {{{STA}, 1}, {{P2P}, 1}}
#  endif

#  ifndef WIFI_HIDL_FEATURE_DISABLE_AP
#    define WIFI_HAL_INTERFACE_COMBINATIONS_AP {{{AP}, 1}}
#  endif
#endif
// clang-format on

// Convert from the legacy format (used by the WIFI_HAL_INTERFACE_COMBINATIONS
// config variable) to a list of ChipConcurrencyCombination objects.
std::vector<IWifiChip::ChipConcurrencyCombination> legacyToChipConcurrencyComboList(
        std::vector<std::vector<IWifiChip::ChipConcurrencyCombinationLimit>> legacyLimits) {
    std::vector<IWifiChip::ChipConcurrencyCombination> combos;
    for (auto& legacyLimit : legacyLimits) {
        IWifiChip::ChipConcurrencyCombination combo = {legacyLimit};
        combos.push_back(combo);
    }
    return combos;
}

#define STA IfaceConcurrencyType::STA
#define AP IfaceConcurrencyType::AP
#define AP_BRIDGED IfaceConcurrencyType::AP_BRIDGED
#define P2P IfaceConcurrencyType::P2P
#undef NAN  // undefine NAN from math.h
#define NAN IfaceConcurrencyType::NAN_IFACE
static const std::vector<IWifiChip::ChipMode> kChipModesPrimary{
        {kMainModeId, legacyToChipConcurrencyComboList({WIFI_HAL_INTERFACE_COMBINATIONS})},
#ifdef WIFI_HAL_INTERFACE_COMBINATIONS_AP
        {chip_mode_ids::kV1Ap,
         legacyToChipConcurrencyComboList({WIFI_HAL_INTERFACE_COMBINATIONS_AP})},
#endif
};

static const std::vector<IWifiChip::ChipMode> kChipModesSecondary{
#ifdef WIFI_HAL_INTERFACE_COMBINATIONS_SECONDARY_CHIP
        {chip_mode_ids::kV3,
         legacyToChipConcurrencyComboList({WIFI_HAL_INTERFACE_COMBINATIONS_SECONDARY_CHIP})},
#endif
};

constexpr char kDebugPresetInterfaceCombinationIdxProperty[] =
        "persist.vendor.debug.wifi.hal.preset_interface_combination_idx";
// List of pre-defined concurrency combinations that can be enabled at runtime via
// setting the property: "kDebugPresetInterfaceCombinationIdxProperty" to the
// corresponding index value.
static const std::vector<std::pair<std::string, std::vector<IWifiChip::ChipMode>>> kDebugChipModes{
        // Legacy combination - No STA/AP concurrencies.
        // 0 - (1 AP) or (1 STA + 1 of (P2P or NAN))
        {"No STA/AP Concurrency",
         {{kMainModeId,
           legacyToChipConcurrencyComboList({{{{AP}, 1}}, {{{STA}, 1}, {{P2P, NAN}, 1}}})}}},

        // STA + AP concurrency
        // 1 - (1 STA + 1 AP) or (1 STA + 1 of (P2P or NAN))
        {"STA + AP Concurrency",
         {{kMainModeId, legacyToChipConcurrencyComboList(
                                {{{{STA}, 1}, {{AP}, 1}}, {{{STA}, 1}, {{P2P, NAN}, 1}}})}}},

        // STA + STA concurrency
        // 2 - (1 STA + 1 AP) or (2 STA + 1 of (P2P or NAN))
        {"Dual STA Concurrency",
         {{kMainModeId, legacyToChipConcurrencyComboList(
                                {{{{STA}, 1}, {{AP}, 1}}, {{{STA}, 2}, {{P2P, NAN}, 1}}})}}},

        // AP + AP + STA concurrency
        // 3 - (1 STA + 2 AP) or (1 STA + 1 of (P2P or NAN))
        {"Dual AP Concurrency",
         {{kMainModeId, legacyToChipConcurrencyComboList(
                                {{{{STA}, 1}, {{AP}, 2}}, {{{STA}, 1}, {{P2P, NAN}, 1}}})}}},

        // STA + STA concurrency and AP + AP + STA concurrency
        // 4 - (1 STA + 2 AP) or (2 STA + 1 of (P2P or NAN))
        {"Dual STA & Dual AP Concurrency",
         {{kMainModeId, legacyToChipConcurrencyComboList(
                                {{{{STA}, 1}, {{AP}, 2}}, {{{STA}, 2}, {{P2P, NAN}, 1}}})}}},

        // STA + STA concurrency
        // 5 - (1 STA + 1 AP (bridged or single) | P2P | NAN), or (2 STA))
        {"Dual STA or STA plus single other interface",
         {{kMainModeId, legacyToChipConcurrencyComboList(
                                {{{{STA}, 1}, {{P2P, NAN, AP, AP_BRIDGED}, 1}}, {{{STA}, 2}}})}}}};

#undef STA
#undef AP
#undef AP_BRIDGED
#undef P2P
#undef NAN

#ifdef WIFI_HIDL_FEATURE_DISABLE_AP_MAC_RANDOMIZATION
#pragma message                                                                   \
        "WIFI_HIDL_FEATURE_DISABLE_AP_MAC_RANDOMIZATION is deprecated; override " \
        "'config_wifi_ap_randomization_supported' in "                            \
        "frameworks/base/core/res/res/values/config.xml in the device overlay "   \
        "instead"
#endif  // WIFI_HIDL_FEATURE_DISABLE_AP_MAC_RANDOMIZATION

WifiFeatureFlags::WifiFeatureFlags() {}

std::vector<IWifiChip::ChipMode> WifiFeatureFlags::getChipModesForPrimary() {
    std::array<char, PROPERTY_VALUE_MAX> buffer;
    auto res = property_get(kDebugPresetInterfaceCombinationIdxProperty, buffer.data(), nullptr);
    // Debug property not set, use the device preset concurrency combination.
    if (res <= 0) return kChipModesPrimary;

    // Debug property set, use one of the debug preset concurrency combination.
    unsigned long idx = std::stoul(buffer.data());
    if (idx >= kDebugChipModes.size()) {
        LOG(ERROR) << "Invalid index set in property: "
                   << kDebugPresetInterfaceCombinationIdxProperty;
        return kChipModesPrimary;
    }
    std::string name;
    std::vector<IWifiChip::ChipMode> chip_modes;
    std::tie(name, chip_modes) = kDebugChipModes[idx];
    LOG(INFO) << "Using debug chip mode: <" << name
              << "> set via property: " << kDebugPresetInterfaceCombinationIdxProperty;
    return chip_modes;
}

std::vector<IWifiChip::ChipMode> WifiFeatureFlags::getChipModes(bool is_primary) {
    return (is_primary) ? getChipModesForPrimary() : kChipModesSecondary;
}

}  // namespace feature_flags
}  // namespace wifi
}  // namespace hardware
}  // namespace android
}  // namespace aidl
