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

#include "llnl/llnl_client_starter.hpp"

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/ndnSIM-module.h"
#include "ns3/ndnSIM/helper/ndn-app-helper.hpp"
#include "ns3/mpi-interface.h"

#include "ns3/ndnSIM/model/ndn-l3-protocol.hpp"
#include "ns3/ndnSIM/model/ndn-net-device-transport.hpp"
#include "ns3/point-to-point-module.h"

 #include "ns3/log.h"
 #include "ns3/string.h"
 #include "ns3/uinteger.h"
 #include "ns3/packet.h"
 #include "ns3/simulator.h"


namespace ns3 {

NS_OBJECT_ENSURE_REGISTERED(LlnlClientStarter);

int
main(int argc, char* argv[])
{
    int nCache = 0;
    int timestamp;
    CommandLine cmd;
    cmd.AddValue("ncache", "Number of Cache Slots", nCache);
    cmd.AddValue("ntime", "timestamp", timestamp);
    cmd.Parse(argc, argv);
    std::cout << "Cache Slots" << nCache << "Timestamp" << timestamp <<  std::endl;
    auto timestamp_str = std::to_string(timestamp);

    Config::SetDefault("ns3::PointToPointNetDevice::DataRate", StringValue("10Gbps"));
    Config::SetDefault("ns3::DropTailQueue::MaxPackets", StringValue("3000000"));
    Config::SetDefault("ns3::PointToPointNetDevice::Mtu", UintegerValue(1500));

    //read the topology
    AnnotatedTopologyReader topologyReader("");
    //topologyReader.SetFileName("/raid/LLNL_ACCESS_LOG/traceroute_data/create_asn_topology_for_ndnsim/ndnsim_large_topology_started_1.0.txt");
    topologyReader.SetFileName("/raid/LLNL_ACCESS_LOG/run_week_"+timestamp_str+"/"+timestamp_str + "week.csv.topology");
    topologyReader.Read();

    std::string dict_name = "/raid/LLNL_ACCESS_LOG/run_week_"+ timestamp_str + "/";

    // install NDN on nodes

    ndn::StackHelper ndnHelper;
    ndnHelper.SetDefaultRoutes(true);

   // ndnHelper.setCsSize(1);
   // 1525,7625,15250
//  ndnHelper.SetOldContentStore("ns3::ndn::cs::Lru", "MaxSize", "140484");
//  ndnHelper.InstallAll();

    // Getting containers for the consumer/producer

    std::vector<std::string> clients;
    //auto clientFilename =  "/raid/LLNL_ACCESS_LOG/client_addresses_started_1.0.txt";
    //auto clientFilename =  "/raid/LLNL_ACCESS_LOG/out.clientlist.txt";
    auto clientFilename =  "/raid/LLNL_ACCESS_LOG/run_week_"+timestamp_str+"/" + timestamp_str +"week.csv.clients";

    std::ifstream is(clientFilename);
    std::string line;
    while(getline(is, line)){
        clients.push_back(line);
    }

    //set cache at the edge
    for (auto x: clients){
        std::cout << "End client :" << x << std::endl;
    }

    std::cout  << "Vector size " << clients.size();


    //set caches everywhere else
    NodeContainer allOtherNodes;
    NodeContainer edgeNodes;
    for (NodeList::Iterator i = NodeList::Begin(); i != NodeList::End(); ++i) {
            if (std::find(clients.begin(), clients.end(), Names::FindName(*i)) != clients.end()){
                std::cout << "EDGE Node ID" << Names::FindName (*i) << std::endl;
                edgeNodes.Add(*i);
            }
            else {
              std::cout << "NETWORK Node ID" << Names::FindName (*i) << std::endl;
              allOtherNodes.Add(*i);
            }
    }

    ndnHelper.SetOldContentStore("ns3::ndn::cs::Nocache");
//    ndnHelper.SetOldContentStore("ns3::ndn::cs::Lru", "MaxSize", std::to_string(nCache));
    //ndnHelper.setCsSize(nCache);
    //ndnHelper.setPolicy("nfd::cs::lru");
    //ndnHelper.SetOldContentStore("ns3::ndn::cs::Lru", "MaxSize", std::to_string(nCache));
    ndnHelper.Install(allOtherNodes);

    //ndnHelper.SetOldContentStore("ns3::ndn::cs::Lru", "MaxSize", std::to_string(nCache));
    //ndnHelper.SetOldContentStore("ns3::ndn::cs::Nocache");
    //ndnHelper.setCsSize(nCache);
    //ndnHelper.setPolicy("nfd::cs::lru");
    ndnHelper.SetOldContentStore("ns3::ndn::cs::Lru", "MaxSize", std::to_string(nCache));
    ndnHelper.Install(edgeNodes);

    // Choosing forwarding strategy
    ndn::StrategyChoiceHelper::InstallAll("/cmip5/app", "/localhost/nfd/strategy/best-route");

    // Installing global routing interface on all nodes
    ndn::GlobalRoutingHelper ndnGlobalRoutingHelper;
    ndnGlobalRoutingHelper.InstallAll();

    //create producer
    Ptr<Node> producer = Names::Find<Node>("1.1.1.1");
    ndn::AppHelper producerApp("ns3::ndn::Producer");
    producerApp.SetAttribute("PayloadSize", StringValue("1"));//doesn't matter really
    producerApp.SetAttribute("Freshness", StringValue("1000"));
    producerApp.SetAttribute("Prefix", StringValue("/cmip5/app"));
    producerApp.Install(producer).Start(Seconds(1));

    //read from a file and create clients


    Ptr<Node> consumers[clients.size()];

    int index = 0;
    ndn::AppHelper consumerApp("LlnlClientStarter");
    for (const auto x: clients) {
        consumers[index] = Names::Find<Node>(x);
        auto ID =  Names::Find<Node>(x)->GetId();
        std::cout << "IP " << x << " ID " << ID << std::endl;
        consumerApp.SetAttribute("IP" , StringValue(x));
        consumerApp.SetAttribute("ID" , StringValue(std::to_string(ID)));
        consumerApp.SetAttribute("DICT" , StringValue(dict_name));
        consumerApp.SetAttribute("TIMESTAMP" , StringValue(timestamp_str));
        //install Consumer App
        consumerApp.Install(consumers[index]).Start(Seconds(3));
        index++;
    }

    //add origin
    ndnGlobalRoutingHelper.AddOrigins("/cmip5/app", producer);

    // Calculate and install FIBs
    auto now = ns3::Simulator::Now().To(ns3::Time::S);
    std::cout << "Calculating routes" << now << std::endl;

    ndn::GlobalRoutingHelper::CalculateRoutes();

    auto now1 = ns3::Simulator::Now().To(ns3::Time::S);
    std::cout << "Calculated routes" << now1 << std::endl;


    Simulator::Stop(Seconds(605800));
    Simulator::Run();
    Simulator::Destroy();
    return 0;
}

} // namespace ns3

int
main(int argc, char* argv[])
{
    return ns3::main(argc, argv);
}

