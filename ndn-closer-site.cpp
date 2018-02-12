/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/**
 * Copyright (c) 2011-2015  Regents of the University of California.
 * Copyright (c) 2017 susmit@colostate.edu, chengyu.fan@colostate.edu
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

// ndn-load-balancer.cpp

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/ndnSIM-module.h"

#include "ndn-closer-site/closer-site-strategy.hpp"
#include "llnl/llnl_client_starter.hpp"

using namespace ns3;

using ns3::ndn::StackHelper;
using ns3::ndn::AppHelper;
using ns3::ndn::GlobalRoutingHelper;
using ns3::ndn::StrategyChoiceHelper;
using ns3::AnnotatedTopologyReader;

NS_OBJECT_ENSURE_REGISTERED(LlnlClientStarter);

int
main(int argc, char* argv[])
{

    int nCache = 0;
    int timestamp = 1443689480;
    uint32_t odds = 0;

    CommandLine cmd;
    cmd.AddValue("ncache", "Number of Cache Slots", nCache);
    cmd.AddValue("ntime", "timestamp", timestamp);
    cmd.AddValue("odds", "failure rate on server", odds);
    cmd.Parse(argc, argv);
    std::cout << "Cache Slots " << nCache << "Timestamp " << timestamp << " odds: " << odds << std::endl;

    auto timestamp_str = std::to_string(timestamp);

    Config::SetDefault("ns3::PointToPointNetDevice::DataRate", StringValue("10Gbps"));
    Config::SetDefault("ns3::DropTailQueue::MaxPackets", StringValue("3000000"));
    Config::SetDefault("ns3::PointToPointNetDevice::Mtu", UintegerValue(1500));

    //read the topology
    AnnotatedTopologyReader topologyReader("");
    topologyReader.SetFileName("/raid/ndnSIM_final/ns-3/topo/1443689480week.csv.topology.2.new-topo");
    topologyReader.Read();

    // okay to use the clients file
    std::string dict_name = "/raid/LLNL_ACCESS_LOG/run_week_"+ timestamp_str + "_balancer/";

    // Install NDN stack on all nodes
    StackHelper ndnHelper;
    //ndnHelper.InstallAll();

    ////////////////////////////////
    /////////// Servers ////////////
    ////////////////////////////////
    std::vector<std::string> servers;
    auto serverFilename =  "/raid/ndnSIM_final/ns-3/topo/1443689480week.csv.servers";

    std::ifstream is1(serverFilename);
    std::string line;
    while(getline(is1, line)){
        servers.push_back(line);
    }

    //set cache at the edge
    for (auto x: servers){
        std::cout << "End servers :" << x << std::endl;
    }

    ////////////////////////////////
    /////////// Clients ////////////
    ////////////////////////////////
    std::vector<std::string> clients;
    auto clientFilename =  "/raid/ndnSIM_final/ns-3/topo/1443689480week.csv.clients";

    std::ifstream is2(clientFilename);
    while(getline(is2, line)){
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
    ndnHelper.Install(allOtherNodes);
    ndnHelper.SetOldContentStore("ns3::ndn::cs::Lru", "MaxSize", std::to_string(nCache));
    ndnHelper.Install(edgeNodes);

    // Choosing forwarding strategy
    // Install NDN applications
    std::string prefix = "/cmip5/app";

    ns3::ndn::StrategyChoiceHelper::InstallAll(prefix, "/localhost/nfd/strategy/closer-site");

    // Installing global routing interface on all nodes
    GlobalRoutingHelper ndnGlobalRoutingHelper;
    ndnGlobalRoutingHelper.InstallAll();

    //create producer
    Ptr<Node> producers[servers.size()];
    ns3::ndn::AppHelper producerApp("ns3::ndn::Producer");
    producerApp.SetAttribute("PayloadSize", StringValue("1"));//doesn't matter really
    producerApp.SetAttribute("Freshness", StringValue("1000"));
    producerApp.SetAttribute("Prefix", StringValue("/cmip5/app"));
    int index = 0;
    for (const auto x: servers) {
        producers[index] = Names::Find<Node>(x);
        std::cout << "producer " << x << " on " << producers[index]->GetId() << std::endl;
        producerApp.Install(producers[index]).Start(Seconds(0));
        index++;
    }
    

    Ptr<Node> consumers[clients.size()];
    index = 0;
    ns3::ndn::AppHelper consumerApp("LlnlClientStarter");
    for (const auto x: clients) {
        consumers[index] = Names::Find<Node>(x);
        auto ID =  Names::Find<Node>(x)->GetId();
        std::cout << "IP " << x << " ID " << ID << std::endl;
        consumerApp.SetAttribute("IP" , StringValue(x));
        consumerApp.SetAttribute("ID" , StringValue(std::to_string(ID)));
        consumerApp.SetAttribute("DICT" , StringValue(dict_name));
        consumerApp.SetAttribute("TIMESTAMP" , StringValue(timestamp_str));
        //install Consumer App
        consumerApp.Install(consumers[index]).Start(Seconds(0));
        index++;
    }

    //add origin 
    index = 0;
    for (const auto x: servers) {
        ndnGlobalRoutingHelper.AddOrigins("/cmip5/app", producers[index]);
        index++;
    }

    // Calculate and install FIBs
    auto now = ns3::Simulator::Now().To(ns3::Time::S);
    std::cout << "Calculating routes" << now << std::endl;

    // Calculate and install FIBs
    // http://www.lists.cs.ucla.edu/pipermail/ndnsim/2016-May/002707.html
    GlobalRoutingHelper::CalculateAllPossibleRoutes();
    //GlobalRoutingHelper::CalculateRoutes();

    Simulator::Stop(Seconds(1000));

    Simulator::Run();
    Simulator::Destroy();

    return 0;
}
