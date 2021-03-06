/**********************************************************************
DCC COMMAND STATION FOR ESP32

COPYRIGHT (c) 2019 Mike Dunston

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
  You should have received a copy of the GNU General Public License
  along with this program.  If not, see http://www.gnu.org/licenses
**********************************************************************/

#pragma once

#include "ESP32CommandStation.h"
#include <openlcb/ConfigRepresentation.hxx>
#include <openlcb/MemoryConfig.hxx>
#include <openlcb/TractionCvCdi.hxx>
#include <freertos_drivers/esp32/Esp32WiFiConfiguration.hxx>

namespace openlcb {
    const SimpleNodeStaticValues SNIP_STATIC_DATA = {
        4,
        "github.com/atanisoft (Mike Dunston)",
        "ESP32 Command Station",
        "ESP32-v1",
        VERSION
    };

    /// Modify this value whenever the config needs to be reinitialized on the
    /// node for a firmware update.
    static constexpr uint16_t CANONICAL_VERSION = 0x0130;

    /// Defines the main segment in the configuration CDI. This is laid out at
    /// origin 128 to give space for the ACDI user data at the beginning.
    CDI_GROUP(CommandStationSegment, Segment(MemoryConfigDefs::SPACE_CONFIG),
              Offset(128));
    /// Each entry declares the name of the current entry, then the type and
    /// then optional arguments list.
    CDI_GROUP_ENTRY(internal_config, InternalConfigData);
    /// CV Access via MemoryConfig protocol.
    //CDI_GROUP_ENTRY(cv, TractionShortCvSpace);
    CDI_GROUP_ENTRY(wifi, WiFiConfiguration, Name("WiFi Configuration"));
    CDI_GROUP_END();

    /// This segment is only needed temporarily until there is program code to set
    /// the ACDI user data version byte.
    CDI_GROUP(VersionSeg, Segment(MemoryConfigDefs::SPACE_CONFIG),
        Name("Version information"));
    CDI_GROUP_ENTRY(acdi_user_version, Uint8ConfigEntry,
        Name("ACDI User Data version"),
        Description("Set to 2 and do not change."));
    CDI_GROUP_END();

    /// The main structure of the CDI. ConfigDef is the symbol used in
    /// src/LCCInterface.cpp to refer to the configuration defined here.
    CDI_GROUP(ConfigDef, MainCdi());
    /// Adds the <identification> tag with the values from SNIP_STATIC_DATA
    /// above.
    CDI_GROUP_ENTRY(ident, Identification);
    /// Adds an <acdi> tag.
    CDI_GROUP_ENTRY(acdi, Acdi);
    /// Adds a segment for changing the values in the ACDI user-defined
    /// space. UserInfoSegment is defined in the system header.
    CDI_GROUP_ENTRY(userinfo, UserInfoSegment);
    /// Adds the main configuration segment.
    CDI_GROUP_ENTRY(seg, CommandStationSegment);
    /// Adds the versioning segment.
    CDI_GROUP_ENTRY(version, VersionSeg);
    CDI_GROUP_END();
}
