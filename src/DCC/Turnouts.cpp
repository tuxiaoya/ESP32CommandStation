/**********************************************************************
ESP32 COMMAND STATION

COPYRIGHT (c) 2017-2019 Mike Dunston

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

/**********************************************************************

The ESP32 Command Station can keep track of the direction of any turnout that is
controlled by a DCC stationary accessory decoder.  All turnouts, as well as any
other DCC accessories connected in this fashion, can always be operated using
the DCC COMMAND STATION Accessory command:

  <a ADDRESS SUBADDRESS ACTIVATE>

However, this general command simply sends the appropriate DCC instruction
packet to the main tracks to operate connected accessories. It does not store
or retain any information regarding the current status of that accessory.

To have this sketch store and retain the direction of DCC-connected turnouts, as
well as automatically invoke the required <a> command as needed, first
define/edit/delete such turnouts using the following variations of the "T"
command:
  <T ID ADDRESS SUBADDRESS>:   creates a new turnout ID, with specified ADDRESS
                               and SUBADDRESS. If turnout ID already exists, it
                               is updated with specificed ADDRESS and SUBADDRESS
      returns: <O> if successful and <X> if unsuccessful (e.g. out of memory)

  <T ID>:                      deletes definition of turnout ID
      returns: <O> if successful and <X> if unsuccessful (e.g. ID does not exist)

  <T>:                         lists all defined turnouts
      returns: <H ID ADDRESS SUBADDRESS THROW> for each defined turnout or <X>
               if no turnouts defined
where
  ID:         the numeric ID (0-32767) of the turnout to control
  ADDRESS:    the primary address of the decoder controlling this turnout (0-511)
  SUBADDRESS: the subaddress of the decoder controlling this turnout (0-3)

Once all turnouts have been properly defined, use the <E> command to store their
definitions to the ESP32. If you later make edits/additions/deletions to the
turnout definitions, you must invoke the <E> command if you want those new
definitions updated on the ESP32. You can also clear everything stored on the
ESP32 by invoking the <e> command.

To "throw" turnouts that have been defined use:
  <T ID THROW>:    sets turnout ID to either the "thrown" or "unthrown" position
        returns: <H ID THROW>, or <X> if turnout ID does not exist
where
  ID:     the numeric ID (0-32767) of the turnout to control
  THROW:  0 (unthrown) or 1 (thrown)

When controlled as such, the Arduino updates and stores the direction of each
Turnout on the ESP32 so that it is retained even without power. A list of the
current directions of each Turnout in the form <H ID THROW> is generated by this
sketch whenever the <s> status command is invoked. This provides an efficient
way of initializing the directions of any Turnouts being monitored or controlled
by a separate interface or GUI program.
**********************************************************************/

LinkedList<Turnout *> turnouts([](Turnout *turnout) {delete turnout; });

static constexpr const char * TURNOUTS_JSON_FILE = "turnouts.json";

static constexpr const char *TURNOUT_TYPE_STRINGS[] = {
  "LEFT",
  "RIGHT",
  "WYE",
  "MULTI"
};

void TurnoutManager::init() {
  LOG(INFO, "[Turnout] Initializing turnout list");
  JsonObject &root = configStore.load(TURNOUTS_JSON_FILE);
  JsonVariant count = root[JSON_COUNT_NODE];
  uint16_t turnoutCount = count.success() ? count.as<int>() : 0;
  LOG(INFO, "[Turnout] Found %d turnouts", turnoutCount);
  InfoScreen::replaceLine(INFO_SCREEN_ROTATING_STATUS_LINE, F("Found %02d Turnouts"), turnoutCount);
  if(turnoutCount > 0) {
    for(auto turnout : root.get<JsonArray>(JSON_TURNOUTS_NODE)) {
      turnouts.add(new Turnout(turnout.as<JsonObject &>()));
    }
  }
}

void TurnoutManager::clear() {
  turnouts.free();
  store();
}

uint16_t TurnoutManager::store() {
  JsonObject &root = configStore.createRootNode();
  JsonArray &array = root.createNestedArray(JSON_TURNOUTS_NODE);
  uint16_t turnoutStoredCount = 0;
  for (const auto& turnout : turnouts) {
    turnout->toJson(array.createNestedObject());
    turnoutStoredCount++;
  }
  root[JSON_COUNT_NODE] = turnoutStoredCount;
  configStore.store(TURNOUTS_JSON_FILE, root);
  return turnoutStoredCount;
}

bool TurnoutManager::setByID(uint16_t id, bool thrown) {
  auto turnout = getTurnoutByID(id);
  if(turnout) {
    turnout->set(thrown);
    return true;
  }
  LOG(WARNING, "[Turnout %d] Unable to set state, turnout not found", id);
  return false;
}

bool TurnoutManager::toggleByID(uint16_t id) {
  auto turnout = getTurnoutByID(id);
  if(turnout) {
    turnout->toggle();
    return true;
  }

  LOG(WARNING, "[Turnout %d] Unable to set state, turnout not found", id);
  return false;
}

bool TurnoutManager::toggleByAddress(uint16_t address) {
  auto turnout = getTurnoutByAddress(address);
  if(turnout) {
    turnout->toggle();
    return true;
  }
  LOG(WARNING, "[Turnout addr:%d] Unable to set state, turnout not found", address);
  return false;
}

void TurnoutManager::getState(JsonArray & array, bool readableStrings) {
  for (const auto& turnout : turnouts) {
    JsonObject &json = array.createNestedObject();
    turnout->toJson(json, readableStrings);
  }
}

void TurnoutManager::showStatus() {
  for (const auto& turnout : turnouts) {
    turnout->showStatus();
  }
}

Turnout *TurnoutManager::createOrUpdate(const uint16_t id, const uint16_t address, const int8_t index, const TurnoutType type) {
  Turnout *turnout = getTurnoutByID(id);
  if(turnout) {
    turnout->update(address, index, type);
  } else {
    turnout = new Turnout(id, address, index, false, type);
    turnouts.add(turnout);
  }
  return turnout;
}

bool TurnoutManager::removeByID(const uint16_t id) {
  Turnout *turnout = getTurnoutByID(id);
  if(turnout) {
    LOG(VERBOSE, "[Turnout %d] Deleted", turnout->getID());
    turnouts.remove(turnout);
    return true;
  }
  return false;
}

bool TurnoutManager::removeByAddress(const uint16_t address) {
  Turnout *turnout = getTurnoutByAddress(address);
  if(turnout) {
    LOG(VERBOSE, "[Turnout %d] Deleted as it used address %d", turnout->getID(), address);
    turnouts.remove(turnout);
    return true;
  }
  return false;
}

Turnout *TurnoutManager::getTurnoutByIndex(const uint16_t index) {
  Turnout *retval = nullptr;
  uint16_t currentIndex = 0;
  for (const auto& turnout : turnouts) {
    if(currentIndex == index) {
      retval = turnout;
    }
    currentIndex++;
  }
  return retval;
}

Turnout *TurnoutManager::getTurnoutByID(const uint16_t id) {
  Turnout *retval = nullptr;
  for (const auto& turnout : turnouts) {
    if(turnout->getID() == id) {
      retval = turnout;
    }
  }
  return retval;
}

Turnout *TurnoutManager::getTurnoutByAddress(const uint16_t address) {
  Turnout *retval = nullptr;
  for (const auto& turnout : turnouts) {
    if(turnout->getAddress() == address) {
      retval = turnout;
    }
  }
  return retval;
}

uint16_t TurnoutManager::getTurnoutCount() {
  return turnouts.length();
}

void calculateTurnoutBoardAddressAndIndex(uint16_t *boardAddress, uint8_t *boardIndex, uint16_t address) {
  *boardAddress = (address + 3) / 4;
  *boardIndex = (address - (*boardAddress * 4)) + 3;
}

Turnout::Turnout(uint16_t turnoutID, uint16_t address, int8_t index,
  bool thrown, TurnoutType type) : _turnoutID(turnoutID),
  _address(address), _index(index), _boardAddress(0), _thrown(thrown),
  _type(type) {
  if(index == -1) {
    // convert the provided decoder address to a board address and accessory index
    calculateTurnoutBoardAddressAndIndex(&_boardAddress, &_index, _address);
    LOG(INFO, "[Turnout %d] Created using DCC address %d as type %s and initial state of %s",
      _turnoutID, _address, TURNOUT_TYPE_STRINGS[_type],
      _thrown ? JSON_VALUE_THROWN : JSON_VALUE_CLOSED);
  } else {
    LOG(INFO, "[Turnout %d] Created using address %d:%d as type %s and initial state of %s",
      _turnoutID, _address, _index, TURNOUT_TYPE_STRINGS[_type],
      _thrown ? JSON_VALUE_THROWN : JSON_VALUE_CLOSED);
  }
}

Turnout::Turnout(JsonObject &json) {
  _turnoutID = json.get<int>(JSON_ID_NODE);
  _address = json.get<int>(JSON_ADDRESS_NODE);
  _index = json.get<int>(JSON_SUB_ADDRESS_NODE);
  _thrown = json.get<bool>(JSON_STATE_NODE);
  _type = (TurnoutType)json.get<int>(JSON_TYPE_NODE);
  _boardAddress = 0;
  if(json.get<int>(JSON_SUB_ADDRESS_NODE) == -1) {
    // convert the provided decoder address to a board address and accessory index
    calculateTurnoutBoardAddressAndIndex(&_boardAddress, &_index, _address);
    LOG(VERBOSE, "[Turnout %d] Loaded using DCC address %d as type %s and last known state of %s",
      _turnoutID, _address, TURNOUT_TYPE_STRINGS[_type],
      _thrown ? JSON_VALUE_THROWN : JSON_VALUE_CLOSED);
  } else {
    LOG(VERBOSE, "[Turnout %d] Loaded using address %d:%d as type %s and last known state of %s",
      _turnoutID, _address, _index, TURNOUT_TYPE_STRINGS[_type],
      _thrown ? JSON_VALUE_THROWN : JSON_VALUE_CLOSED);
  }
}

void Turnout::update(uint16_t address, int8_t index, TurnoutType type) {
  _address = address;
  _index = index;
  _type = type;
  if(index == -1) {
    // convert the provided decoder address to a board address and accessory index
    calculateTurnoutBoardAddressAndIndex(&_boardAddress, &_index, _address);
    LOG(VERBOSE, "[Turnout %d] Updated to use DCC address %d and type %s",
      _turnoutID, _address, TURNOUT_TYPE_STRINGS[_type]);
  } else {
    LOG(VERBOSE, "[Turnout %d] Updated to address %d:%d and type %s",
      _turnoutID, _address, _index, TURNOUT_TYPE_STRINGS[_type]);
  }
}

void Turnout::toJson(JsonObject &json, bool readableStrings) {
  json[JSON_ID_NODE] = _turnoutID;
  json[JSON_ADDRESS_NODE] = _address;
  json[JSON_BOARD_ADDRESS_NODE] = _boardAddress;
  if(_boardAddress) {
    json[JSON_SUB_ADDRESS_NODE] = -1;
  } else {
    json[JSON_SUB_ADDRESS_NODE] = _index;
  }
  if(readableStrings) {
    if(_thrown) {
      json[JSON_STATE_NODE] = JSON_VALUE_THROWN;
    } else {
      json[JSON_STATE_NODE] = JSON_VALUE_CLOSED;
    }
  } else {
    json[JSON_STATE_NODE] = _thrown;
  }
  json[JSON_TYPE_NODE] = (int)_type;
}

void Turnout::set(bool thrown, bool sendDCCPacket) {
  _thrown = thrown;
  if(sendDCCPacket) {
    std::vector<String> args;
    args.push_back(String(_boardAddress));
    args.push_back(String(_index));
    args.push_back(String(_thrown));
    DCCPPProtocolHandler::getCommandHandler("a")->process(args);
  }
  wifiInterface.print(F("<H %d %d>"), _turnoutID, _thrown);
  LOG(VERBOSE, "[Turnout %d] Set to %s", _turnoutID,
    _thrown ? JSON_VALUE_THROWN : JSON_VALUE_CLOSED);
}

void Turnout::showStatus() {
  wifiInterface.print(F("<H %d %d %d %d>"), _turnoutID, _address, _index, _thrown);
}

void TurnoutCommandAdapter::process(const std::vector<String> arguments) {
  if(arguments.empty()) {
    // list all turnouts
    TurnoutManager::showStatus();
  } else {
    uint16_t turnoutID = arguments[0].toInt();
    if (arguments.size() == 1 && TurnoutManager::removeByID(turnoutID)) {
      // delete turnout
      wifiInterface.send(COMMAND_SUCCESSFUL_RESPONSE);
    } else if (arguments.size() == 2 && TurnoutManager::setByID(turnoutID, arguments[1].toInt() == 1)) {
      // throw turnout
    } else if (arguments.size() == 3) {
      // create/update turnout
      TurnoutManager::createOrUpdate(turnoutID, arguments[1].toInt(), arguments[2].toInt());
      wifiInterface.send(COMMAND_SUCCESSFUL_RESPONSE);
    } else {
      wifiInterface.send(COMMAND_FAILED_RESPONSE);
    }
  }
}

void TurnoutExCommandAdapter::process(const std::vector<String> arguments) {
  if(arguments.empty()) {
    wifiInterface.send(COMMAND_FAILED_RESPONSE);
  } else {
    int32_t turnoutID = arguments[0].toInt();
    if(turnoutID > 0) {
      auto turnout = TurnoutManager::getTurnoutByID(arguments[0].toInt());
      if(turnout && arguments.size() == 1) {
        turnout->toggle();
      } else if(turnout) {
        turnout->setType((TurnoutType)arguments[1].toInt());
      } else {
        wifiInterface.send(COMMAND_FAILED_RESPONSE);
      }
    } else {
      auto turnout = TurnoutManager::getTurnoutByAddress(arguments[1].toInt());
      if(turnout) {
        turnout->setType((TurnoutType)arguments[2].toInt());
      } else {
        TurnoutManager::createOrUpdate(TurnoutManager::getTurnoutCount() + 1, arguments[1].toInt(), -1, (TurnoutType)arguments[2].toInt());
      }
    }
  }
}

void AccessoryCommand::process(const std::vector<String> arguments) {
  if(dccSignal[DCC_SIGNAL_OPERATIONS]->isEnabled()) {
    std::vector<uint8_t> packetBuffer;
    uint16_t boardAddress = arguments[0].toInt();
    uint8_t boardIndex = arguments[1].toInt();
    bool activate = arguments[2].toInt() == 1;
    LOG(VERBOSE, "[Turnout] DCC Accessory Packet %d:%d state: %d", boardAddress, boardIndex, activate);
    // first byte is of the form 10AAAAAA, where AAAAAA represent 6 least
    // signifcant bits of accessory address
    packetBuffer.push_back(0x80 + boardAddress % 64);
    // second byte is of the form 1AAACDDD, where C should be 1, and the least
    // significant D represent activate/deactivate
    packetBuffer.push_back(((((boardAddress / 64) % 8) << 4) +
      (boardIndex % 4 << 1) + activate) ^ 0xF8);
    dccSignal[DCC_SIGNAL_OPERATIONS]->loadPacket(packetBuffer, 1);
  }
}
