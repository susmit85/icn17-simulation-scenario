/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/**
 * Copyright (c) 2011-2015  Regents of the University of California.
 * Copyright (c) 2016 susmit@colostate.edu, chengyu.fan@colostate.edu
 *
 *
 * This file is part of ndnSIM. See AUTHORS for complete list of ndnSIM authors and
 * contributors.
 *
 * ndnSIM is free software: you can redistribute it and/or modify it under the terms
 * of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * ndnSIM is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * ndnSIM, e.g., in COPYING.md file.  If not, see <http://www.gnu.org/licenses/>.
 **/
#include "ns3/application.h"
#include "llnl_clients.hpp"
#include "ns3/simulator.h"
#include "ns3/packet.h"

#include "ns3/ndnSIM/helper/ndn-stack-helper.hpp"
#include "ns3/ndnSIM/helper/ndn-fib-helper.hpp"
#include "ns3/ndnSIM/helper/ndn-app-helper.hpp"

#include "ns3/random-variable-stream.h"
#include <iostream>
#include <string>
#include <fstream>
#include <vector>
#include <boost/algorithm/string.hpp>
#include <ndn-cxx/face.hpp>
#include "ns3/core-module.h"


namespace ns3 {
NS_LOG_COMPONENT_DEFINE("LlnlClientStarter");

// Class inheriting from ns3::Application
class LlnlClientStarter : public Application
{
public:
  static TypeId
  GetTypeId()
  {
    static TypeId tid = TypeId("LlnlClientStarter")
      .SetParent<Application>()
      .AddConstructor<LlnlClientStarter>()
      .AddAttribute("IP", "IP of Client node", StringValue("none"), MakeStringAccessor(&LlnlClientStarter::SetIP, &LlnlClientStarter::GetIP), MakeStringChecker())
      .AddAttribute("ID", "ID of Client node", StringValue("none"), MakeStringAccessor(&LlnlClientStarter::SetID, &LlnlClientStarter::GetID), MakeStringChecker())
      .AddAttribute("DICT", "Client DICT Client node", StringValue("none"), MakeStringAccessor(&LlnlClientStarter::SetDICT, &LlnlClientStarter::GetDICT), MakeStringChecker())
      .AddAttribute("TIMESTAMP", "Timestamp", StringValue("none"), MakeStringAccessor(&LlnlClientStarter::SetTimestamp, &LlnlClientStarter::GetTimestamp), MakeStringChecker())
      ;
    return tid;
  }

 void
 SetTimestamp(const std::string& value)
 {
  	Timestamp = value;
 }

 std::string
 GetTimestamp() const
 {
	return Timestamp;
 }


 void
 SetIP(const std::string& value)
 {
  	IP = value;
 }

 std::string
 GetIP() const
 {
	return IP;
 }

 void
 SetDICT(const std::string& value)
 {
  	DICT = value;
 }

 std::string
 GetDICT() const
 {
	return DICT;
 }

 void
 SetID(const std::string& value)
 {
  	ID = value;
 }

 std::string
 GetID() const
 {
	return ID;
 }

protected:
  // inherited from Application base class.
  virtual void
  StartApplication()
  {

    // Create an instance of the app, and passing the dummy version of KeyChain (no real signing)
    m_instance.reset(new app::LlnlConsumerWithTimer(IP, ID, DICT, Timestamp));
    m_instance->run(); // can be omitted
  }

  virtual void
  StopApplication()
  {
    // Stop and destroy the instance of the app
    m_instance.reset();
  }

private:
  std::unique_ptr<app::LlnlConsumerWithTimer> m_instance;
  std::string IP;
  std::string ID;
  std::string DICT;
  std::string Timestamp;

};

} // namespace ns3
