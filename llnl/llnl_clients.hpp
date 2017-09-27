/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/**
 * Copyright (c) 2011-2015  Regents of the University of California.
 * Copyright (c) 2016 susmit@colostate.edu, chengyu.fan@colostate.edu
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

// llnl_clients.cpp

#include <memory>

#include "ns3/log.h"
#include "ns3/string.h"
#include "ns3/uinteger.h"
#include "ns3/packet.h"
#include "ns3/simulator.h"

#include "model/ndn-l3-protocol.hpp"
#include "helper/ndn-fib-helper.hpp"
#include "utils/ndn-ns3-packet-tag.hpp"

#include <string>
#include <fstream>
#include <vector>
#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/random.hpp>

#include <iostream>

#include <ndn-cxx/face.hpp>
#include <ndn-cxx/interest.hpp>
#include <ndn-cxx/security/key-chain.hpp>
#include <ndn-cxx/util/scheduler.hpp>


#include <ndn-cxx/lp/tags.hpp>

#include <ns3/node.h>

namespace app {
//namespace ndn {
NS_LOG_COMPONENT_DEFINE("ndn.LlnlConsumer");


class LlnlConsumerWithTimer
{
public:
    LlnlConsumerWithTimer(std::string PassedIP, std::string PassedID, std::string PassedDict, std::string PassedTimestamp)
        : m_face(m_ioService) // Create face with io_service object
        , m_scheduler(m_ioService)

    {
        IP = PassedIP;
        ID = PassedID;
        dict_name = PassedDict;
        timestamp = PassedTimestamp;

//        auto nodeID = ns3::Simulator::GetContext();

    }

    void
    run()
    {
        // read the ip, read corresponding file, schedule interests
        std::cout << "IP = " << IP << " ID = " << ID << "Dict Name =" << dict_name << std::endl;

        long now = ns3::Simulator::Now().GetSeconds();
        // Schedule send of first interest
        auto fileName = dict_name + IP + ".client.txt";
        //auto fileName = "src/ndnSIM/examples/llnl/data/" + IP + ".small.txt";
       // auto fileName = "/raid/LLNL_ACCESS_LOG/LLNL_access_logging_extract.csv0.1";
        //auto fileName = "/raid/LLNL_ACCESS_LOG/LLNL_access_log_ASN.csv1.0";
        std::cout << "Starting Simulation at " << now << " filename " << fileName << std::endl;
        std::ifstream is(fileName);
        std::string line;
        std::vector<std::string> parts;
        long count = 0;
        while(getline(is, line)) {
                //if ip in line
                if (line.find(IP) != std::string::npos) {
                //        std::cout << "found!" << IP <<  '\n';
                //        std::cout << "Read line " << line <<  " IP " << IP <<  std::endl;

//            if (count < 20) {
                boost::split(parts, line, boost::is_any_of("\t"));

                auto data_name = parts[3];
                auto data_size = boost::lexical_cast<float>(parts[14]);

                auto IntName = "/cmip5/app/" + data_name;

                auto maxSegment = std::ceil(data_size/(segmentSize));
                if (maxSegment <= 0 ) {
                    maxSegment = 1;
                }

                auto ndnName = ndn::Name(IntName).appendSegment(maxSegment).appendSegment(0);
                ndn::Interest interest(ndnName);
                interest.refreshNonce();

                uint32_t initNonce = interest.getNonce();


                //ignore
                if(data_size == -2) {
                    //don't do anything
                    continue;
                }
                else {
                    //a new request
                    record_segmentNums[initNonce] = 0;
                    std::cout << "Data Size " << data_size <<  " Max segments = " << maxSegment << std::endl;
                }

                int Pipeline = 0;

                if (maxSegment < PipelineSize) {
                    Pipeline = maxSegment;
                }
                else
                {
                    Pipeline = PipelineSize;
                }

                std::cout << "Pipeline size = " << Pipeline << std::endl;

                for (auto segmentNum = 0; segmentNum < Pipeline; segmentNum++) {

                    std::cout.precision(17);
                    record_segmentNums[initNonce]  = record_segmentNums[initNonce]+1;
//                    std::cout << "Updating (Name, nonce) pair: " << IntName << " " <<   record_segmentNums[initNonce] << std::endl;

                    ndnName = ndnName.getPrefix(-1).appendSegment(segmentNum);

//                 auto time = boost::lexical_cast<long>(parts[8])/100 + segmentNum ;
                   auto time = (boost::lexical_cast<long>(parts[9])-std::stoi(timestamp));
//                  auto time = boost::lexical_cast<long>(parts[8])-1324339471/100000;
                    //set lifetime to seconds*1000/10^9 = seconds/10^6
                    interest.setName(ndnName);
                    //1 sec = 1000 secs
                    interest.setInterestLifetime(ndn::time::seconds(100000));
                    interest.setMustBeFresh(true);
                    interest.setNonce(initNonce);
                    std::cout <<  "Scheduling " <<  interest.getName() << " at node "  << ID << " at time " << time <<  "init nonce " <<  initNonce << std::endl;
                    m_scheduler.scheduleEvent(ndn::time::seconds(time),
                                              bind(&LlnlConsumerWithTimer::delayedInterest, this, interest));
                }
               //}
            //    else { break; }


            count++;
            }
        }
        std::cout << "Processing event " << std::endl;

        //m_ioService.run();
        // Alternatively, m_face.processEvents() can also be called.
        m_face.processEvents();
    }

private:
    void
    onData(const ndn::Interest& interest, const ndn::Data& data)
    {
            long now_in_sec = ns3::Simulator::Now().GetSeconds();
            auto now = ns3::Simulator::Now().To(ns3::Time::S);
            auto interestNonce = interest.getNonce();

            auto hopCountTag = data.getTag<ndn::lp::HopCountTag>();
            /*if (hopCountTag != nullptr) { // e.g., packet came from local node's cache
                hopCount = *hopCountTag;
                //		std::cout << "Hop count: " << *hopCountTag << std::endl;
            }
            else {
                std::cout << "Packet tag doesn't exist for " << data.getName() <<  std::endl;
            }*/


            //lookup how many segments we need to request
            std::cout << "Time: " << now << ",node:" << ID  <<  ",func:Consumer:onData" << ",IP:" <<  IP << " ,Interest Nonce " << interestNonce
            << " ,Data Name "  << data.getName() << ",Hop Count " << *hopCountTag << std::endl;

            //we don't need the current segment number, just the latest segment number
            ndn::Name newInterestName = data.getName().getPrefix(-3);
            auto maxSeg = 0;
            try{
                maxSeg = data.getName().get(-3).toNumber();
            } catch(std::exception& e){std::cout<< "Passing bad segment number" << data.getName() << e.what() << std::endl;}

            auto latestSeg = record_segmentNums[interestNonce];


            //      std::cout << "Look up(Name, nonce) pair: " << newInterestName.toUri() << " " << nonce << "maxSeg = " << maxSeg << " LatestSeg " << latestSeg << std::endl;

            if (latestSeg < maxSeg) {
                record_segmentNums[interestNonce]  = record_segmentNums[interestNonce]+1;
                std::cout << "Latest Segment " << latestSeg << "Max segment " << maxSeg << "New Interest Segment " << record_segmentNums[interestNonce] << std::endl;

                newInterestName.appendSegment(maxSeg).appendSegment(latestSeg+1);
                ndn::Interest newInterest(newInterestName);
                newInterest.setNonce(interestNonce);

                auto newTime = now_in_sec;
                m_scheduler.scheduleEvent(ndn::time::seconds(newTime),
                                          bind(&LlnlConsumerWithTimer::delayedInterest, this, newInterest));
            }

    }



    void
    onNack(const ndn::Interest& interest, const ndn::lp::Nack& nack)
    {
        NS_LOG_INFO("Received Nack with reason " << nack.getReason()
                    << " for interest " << interest.getName() << " at " << IP);
        auto now = ns3::Simulator::Now().To(ns3::Time::S);
        std::cout << "Time: " << now << ",node:" << ID  <<  ",func:Consumer:onNack" << ",IP:" <<  IP << ",Interest Name "  << interest.getName() << " Reason " << nack.getReason() << std::endl;
    }




    void
    onTimeout(const ndn::Interest& interest)
    {
        NS_LOG_INFO( "Timeout " << interest << " at " << IP );
        auto newInterest = ndn::Interest(interest.getName());
        newInterest.setInterestLifetime(ndn::time::seconds(1));
        newInterest.setMustBeFresh(true);
        //newInterest.refreshNonce();
        NS_LOG_INFO("New Interest " << newInterest << " with refreshed nonce " << newInterest.getNonce() << " at " << IP);
        auto now = ns3::Simulator::Now().To(ns3::Time::S);
        std::cout << "Time: " << now << ",node:" << ID  <<  ",func:Consumer:onTimeout" << ",IP:" <<  IP << ",Interest Name "  << interest.getName() << std::endl;

        m_scheduler.scheduleEvent(ndn::time::seconds(1),
                                  bind(&LlnlConsumerWithTimer::delayedInterest, this, newInterest));
    }

    void
    delayedInterest(const ndn::Interest& interest)
    {
        m_face.expressInterest(interest,
                               bind(&LlnlConsumerWithTimer::onData, this, _1, _2),
                               bind(&LlnlConsumerWithTimer::onNack, this, _1, _2),
                               bind(&LlnlConsumerWithTimer::onTimeout, this, _1));

        NS_LOG_INFO("Sending " << interest << " from " << IP);
        auto now = ns3::Simulator::Now().To(ns3::Time::S);
        std::cout << "Time: " << now << ",node:" << ID  <<  ",func:Consumer:delayedInterest" << ",IP:" <<  IP << ",Interest Name "  << interest.getName() << " Nonce "
         << interest.getNonce() << std::endl ;
    }

private:
    // Explicitly create io_service object, which can be shared between Face and Scheduler
    boost::asio::io_service m_ioService;
    ndn::Face m_face;
    ndn::Scheduler m_scheduler;
    std::string IP;
    std::string ID;
    std::string dict_name;
    std::string timestamp;
    uint32_t PipelineSize = 64; //minimum 2
    //init nonce, latest segment
    std::map<uint32_t, uint32_t> record_segmentNums;
    uint32_t segmentSize = 100000000; //100MB
};
}//ndn
