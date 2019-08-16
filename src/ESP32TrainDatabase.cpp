/**********************************************************************
ESP32 COMMAND STATION

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

#include "ESP32CommandStation.h"
#include "ESP32TrainDatabase.h"

#include "TrainDbCdi.hxx"

namespace commandstation
{
// JSON serialization mappings for the commandstation::DccMode enum
NLOHMANN_JSON_SERIALIZE_ENUM(DccMode,
{
  { DCCMODE_DEFAULT,     "DCC" },
  { DCCMODE_OLCBUSER,    "DCC-OlcbUser" },
  { MARKLIN_DEFAULT,     "Marklin" },
  { MARKLIN_OLD,         "Marklin (v1)" },
  { MARKLIN_NEW,         "Marklin (v2, f0-f4)" },
  { MARKLIN_TWOADDR,     "Marklin (v2, f0-f8)" },
  { MFX,                 "Marklin (MFX)" },
  { DCC_DEFAULT,         "DCC"},
  { DCC_14,              "DCC (14 speed step)"},
  { DCC_28,              "DCC (28 speed step)"},
  { DCC_128,             "DCC (128 speed step)"},
  { DCC_14_LONG_ADDRESS, "DCC (14 speed step, long address)"},
  { DCC_28_LONG_ADDRESS, "DCC (28 speed step, long address)"},
  { DCC_128_LONG_ADDRESS,"DCC (128 speed step, long address)"},
});

// JSON serialization mappings for the commandstation::Symbols enum
NLOHMANN_JSON_SERIALIZE_ENUM(Symbols,
{
  { FN_NONEXISTANT,   "N/A" },
  { LIGHT,            "Light" },
  { BEAMER,           "Beamer" },
  { BELL,             "Bell" },
  { HORN,             "Horn" },
  { SHUNT,            "Shunting mode" },
  { PANTO,            "Pantograph" },
  { SMOKE,            "Smoke" },
  { ABV,              "Momentum On/Off" },
  { WHISTLE,          "Whistle" },
  { SOUND,            "Sound" },
  { FNT11,            "Generic Function" },
  { SPEECH,           "Announce" },
  { ENGINE,           "Engine" },
  { LIGHT1,           "Light1" },
  { LIGHT2,           "Light2" },
  { TELEX,            "Coupler" },
  { FN_UNKNOWN,       "Unknown" },
  { MOMENTARY,        "momentary" },
  { FNP,              "fnp" },
  { SOUNDP,           "soundp" },
  { FN_UNINITIALIZED, "uninit" },
})
}

namespace esp32cs
{

using nlohmann::json;

static constexpr const char * const TRAIN_CDI_FILE =
  LCC_PERSISTENT_CONFIG_DIR "/train.xml";

static constexpr const char * const TEMP_TRAIN_CDI_FILE =
  LCC_PERSISTENT_CONFIG_DIR "/tmptrain.xml";

// copied from OpenMRNLite.h as a customized version to avoid adding the passed
// config object to the stack. We only need to generate the CDI at this point
// as it will be managed by the AllTrainsNode instead.
template <class ConfigDef>
void create_config_descriptor_xml(const ConfigDef &config
                                , const char *filename)
{
  string cdi_string;
  ConfigDef cfg(config.offset());
  cfg.config_renderer().render_cdi(&cdi_string);

  bool need_write = false;
  FILE *ff = fopen(filename, "rb");
  if (!ff)
  {
    need_write = true;
  }
  else
  {
    fclose(ff);
    string current_str = read_file_to_string(filename);
    if (current_str != cdi_string)
    {
      need_write = true;
    }
  }
  if (need_write)
  {
    LOG(INFO, "Updating CDI file %s (len %u)", filename,
        cdi_string.size());
    write_string_to_file(filename, cdi_string);
  }
}

// converts a Esp32PersistentTrainData to a json object
void to_json(json& j, const Esp32PersistentTrainData& t)
{
  j = json({
    { JSON_NAME_NODE, t.name },
    { JSON_ADDRESS_NODE, t.address },
    { JSON_IDLE_ON_STARTUP_NODE, t.automatic_idle },
    { JSON_DEFAULT_ON_THROTTLE_NODE, t.show_on_limited_throttles },
    { JSON_FUNCTIONS_NODE, t.functions },
    { JSON_MODE_NODE, t.mode },
  });
}

// converts a json payload to a Esp32PersistentTrainData object
void from_json(const json& j, Esp32PersistentTrainData& t)
{
  j.at(JSON_NAME_NODE).get_to(t.name);
  j.at(JSON_ADDRESS_NODE).get_to(t.address);
  j.at(JSON_IDLE_ON_STARTUP_NODE).get_to(t.automatic_idle);
  j.at(JSON_DEFAULT_ON_THROTTLE_NODE).get_to(t.show_on_limited_throttles);
  j.at(JSON_FUNCTIONS_NODE).get_to(t.functions);
  j.at(JSON_MODE_NODE).get_to(t.mode);
}

Esp32TrainDbEntry::Esp32TrainDbEntry(Esp32PersistentTrainData data)
  : props_(data), maxFn_(data.functions.size())
{
  LOG(INFO, "[Loco:%s] max function: %d", identifier().c_str(), maxFn_);
}

string Esp32TrainDbEntry::identifier()
{
  dcc::TrainAddressType addrType =
    dcc_mode_to_address_type(props_.mode, props_.address);
  if (addrType == dcc::TrainAddressType::DCC_SHORT_ADDRESS ||
      addrType == dcc::TrainAddressType::DCC_LONG_ADDRESS)
  {
    string prefix = "long_address";
    if (addrType == dcc::TrainAddressType::DCC_SHORT_ADDRESS)
    {
      prefix = "short_address";
    }
    if ((props_.mode & DCC_SS_MASK) == 1)
    {
      return StringPrintf("dcc_14/%s/%d", prefix.c_str(), props_.address);
    }
    else if ((props_.mode & DCC_SS_MASK) == 2)
    {
      return StringPrintf("dcc_28/%s/%d", prefix.c_str(), props_.address);
    }
    else
    {
      return StringPrintf("dcc_128/%s/%d", prefix.c_str(), props_.address);
    }
  }
  else if (addrType == dcc::TrainAddressType::MM)
  {
    // TBD: should marklin types be explored further?
    return StringPrintf("marklin/%d", props_.address);
  }
  return StringPrintf("unknown/%d", props_.address);
}

NodeID Esp32TrainDbEntry::get_traction_node()
{
  if (props_.mode == DCCMODE_OLCBUSER)
  {
    // TODO: move this to a constant in TractionDefs
    return 0x050101010000ULL | static_cast<NodeID>(props_.address);
  }
  else
  {
    return TractionDefs::train_node_id_from_legacy(
        dcc_mode_to_address_type(props_.mode, props_.address)
      , props_.address);
  }
}

unsigned Esp32TrainDbEntry::get_function_label(unsigned fn_id)
{
  // if the function id is larger than our max list reject it
  if (fn_id > maxFn_)
  {
    return FN_NONEXISTANT;
  }
  // return the mapping for the function
  return props_.functions[fn_id];
}

#define _FIND_TRAIN(method, value)                      \
std::find_if(knownTrains_.begin(), knownTrains_.end(),  \
  [value](const shared_ptr<Esp32TrainDbEntry> &train)   \
  {                                                     \
    return train->method() == value;                    \
  });

#define FIND_TRAIN_BY_ADDRESS(address) _FIND_TRAIN(get_legacy_address, address)
#define FIND_TRAIN_BY_NODE_ID(node_id) _FIND_TRAIN(get_traction_node, node_id)
#define IS_VALID_TRAIN_ENTRY(entry) entry != knownTrains_.end()

static constexpr const char * TRAIN_DB_JSON_FILE = "trains.json";
static constexpr const char * LEGACY_ROSTER_JSON_FILE = "roster.json";

Esp32TrainDatabase::Esp32TrainDatabase() : dirty_(false)
{
  TrainConfigDef trainCfg(0);
  TrainTmpConfigDef tmpTrainCfg(0);
  create_config_descriptor_xml(trainCfg, TRAIN_CDI_FILE);
  create_config_descriptor_xml(tmpTrainCfg, TEMP_TRAIN_CDI_FILE);
  trainCdiFile_.reset(new ROFileMemorySpace(TRAIN_CDI_FILE));
  tempTrainCdiFile_.reset(new ROFileMemorySpace(TEMP_TRAIN_CDI_FILE));

  LOG(INFO, "[TrainDB] Initializing...");
  if (configStore->exists(TRAIN_DB_JSON_FILE))
  {
    json stored_trains = json::parse(configStore->load(TRAIN_DB_JSON_FILE));
    for (auto &entry : stored_trains)
    {
      knownTrains_.emplace(
        new Esp32TrainDbEntry(entry.get<Esp32PersistentTrainData>()));
    }
  }

  if (configStore->exists(LEGACY_ROSTER_JSON_FILE))
  {
    LOG(INFO, "[TrainDB] Loading Legacy roster file...");
    json roster = json::parse(configStore->load(LEGACY_ROSTER_JSON_FILE));
    for (auto &entry : roster)
    {
      Esp32PersistentTrainData data;
      entry.at(JSON_ADDRESS_NODE).get_to(data.address);
      entry.at(JSON_DESCRIPTION_NODE).get_to(data.name);
      string auto_idle = entry[JSON_IDLE_ON_STARTUP_NODE];
      string limited_throttle = entry[JSON_DEFAULT_ON_THROTTLE_NODE];
      data.automatic_idle = (auto_idle == JSON_VALUE_TRUE);
      data.show_on_limited_throttles = (limited_throttle == JSON_VALUE_TRUE);
      knownTrains_.emplace(new Esp32TrainDbEntry(data));
    }
    // when persisting the database the legacy file should be removed.
    // configStore->remove(LEGACY_ROSTER_JSON_FILE);
    // persistence is TBD
    dirty_ = true;
    legacyEntriesFound_ = true;
  }
  LOG(INFO, "[TrainDB] There are %d entries in the database."
    , knownTrains_.size());
}

shared_ptr<TrainDbEntry> Esp32TrainDatabase::get_entry(unsigned train_id)
{
  OSMutexLock l(&knownTrainsLock_);
  auto entry = FIND_TRAIN_BY_ADDRESS(train_id)
  if (IS_VALID_TRAIN_ENTRY(entry))
  {
    return *entry;
  }
  return nullptr;
}

shared_ptr<TrainDbEntry> Esp32TrainDatabase::find_entry(NodeID node_id
                                                      , unsigned hint)
{
  OSMutexLock l(&knownTrainsLock_);
  auto entry = FIND_TRAIN_BY_NODE_ID(node_id)
  if (IS_VALID_TRAIN_ENTRY(entry))
  {
    return *entry;
  }
  return nullptr;
}

unsigned Esp32TrainDatabase::add_dynamic_entry(TrainDbEntry* temp_entry)
{
  uint16_t address = temp_entry->get_legacy_address();
  DccMode mode = temp_entry->get_legacy_drive_mode();

  // discard the provided entry as we use our own custom type
  delete temp_entry;

  {
    OSMutexLock l(&knownTrainsLock_);
    auto entry = FIND_TRAIN_BY_ADDRESS(address)

    if (IS_VALID_TRAIN_ENTRY(entry))
    {
      return (*entry)->get_legacy_address();
    }

    // create the new entry
    knownTrains_.emplace(
      new Esp32TrainDbEntry(Esp32PersistentTrainData(address, mode)));
    return address;
  }
}

string Esp32TrainDatabase::get_train_list_as_json()
{
  OSMutexLock l(&knownTrainsLock_);
  // If we don't have any trains in our db return an empty set to the caller.
  if (knownTrains_.empty())
  {
    return "[]";
  }
  // convert our db into a json set and return it to the caller.
  json j;
  for (auto entry : knownTrains_)
  {
    j.push_back(entry->get_data());
  }
  return j.dump();
}

string Esp32TrainDatabase::get_train_as_json(uint16_t address)
{
  OSMutexLock l(&knownTrainsLock_);
  auto entry = FIND_TRAIN_BY_ADDRESS(address)
  if (IS_VALID_TRAIN_ENTRY(entry))
  {
    json j = (*entry)->get_data();
    return j.dump();
  }
  return "{}";
}

} // namespace esp32cs