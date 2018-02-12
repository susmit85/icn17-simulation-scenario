/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/**
 * Copyright (c) 2014-2016,  Regents of the University of California,
 *                           Arizona Board of Regents,
 *                           Colorado State University,
 *                           University Pierre & Marie Curie, Sorbonne University,
 *                           Washington University in St. Louis,
 *                           Beijing Institute of Technology,
 *                           The University of Memphis.
 *
 * Copyright (c) 2017 susmit@colostate.edu, chengyu.fan@colostate.edu
 *
 * This file is part of NFD (Named Data Networking Forwarding Daemon).
 * See AUTHORS.md for complete list of NFD authors and contributors.
 *
 * NFD is free software: you can redistribute it and/or modify it under the terms
 * of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * NFD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * NFD, e.g., in COPYING.md file.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "closer-site-strategy.hpp"
#include "fw/algorithm.hpp"

#include <ndn-cxx/util/time.hpp>

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/identity.hpp>
#include <boost/multi_index/mem_fun.hpp>


using namespace ndn::time;
using namespace boost::multi_index;

NFD_LOG_INIT("CloserSiteStrategy");
namespace nfd {
namespace fw {

class MyPitInfo;
class MyMeasurementInfo;
class WeightedFace;

class WeightedFace
{
public:

  WeightedFace(Face& face_,
               const milliseconds& delay = milliseconds(0))
    : face(face_)
    , lastDelay(delay)
  {
    calculateWeight();
  }

  bool
  operator<(const WeightedFace& other) const
  {
    if (lastDelay == other.lastDelay)
      return face.getId() < other.face.getId();

    return lastDelay < other.lastDelay;
  }

  uint64_t
  getId() const
  {
    return face.getId();
  }

  static void
  modifyWeightedFaceDelay(WeightedFace& weightedFace,
                          const ndn::time::milliseconds& delay)
  {
    weightedFace.lastDelay = delay;
    weightedFace.calculateWeight();
  }

  static void
  modifyWeightedFaceFlag(WeightedFace& weightedFace)
  {
    weightedFace.updated = true;
  }

  void
  calculateWeight()
  {
    weight = (1.0 * (milliseconds::max() - lastDelay)) / milliseconds::max();
  }

  Face& face;
  ndn::time::milliseconds lastDelay;
  double weight;
  bool updated = false;
};

///////////////////////
// PIT entry storage //
///////////////////////

class MyPitInfo : public StrategyInfo
{
public:
  MyPitInfo()
    : creationTime(system_clock::now())
  {}

  static int constexpr
  getTypeId() { return 9970; }

  system_clock::TimePoint creationTime;
};

///////////////////////////////
// Measurement entry storage //
///////////////////////////////

class MyMeasurementInfo : public StrategyInfo
{
public:

  MyMeasurementInfo() : weightedFaces(new WeightedFaceSet) {}

  void
  updateFaceDelay(const Face& face, const milliseconds& delay);

  uint32_t
  updateStoredNextHops(const fib::NextHopList& nexthops);

  bool
  isFaceUpdated(const Face& face);

  static int constexpr
  getTypeId() { return 9971; }

public:

  struct ByDelay {};
  struct ByFaceId {};

  typedef multi_index_container<
    WeightedFace,
    indexed_by<
      ordered_unique<
        tag<ByDelay>,
        identity<WeightedFace>
        >,
      hashed_unique<
        tag<ByFaceId>,
        const_mem_fun<WeightedFace, uint64_t, &WeightedFace::getId>
        >
      >
    > WeightedFaceSet;

  typedef WeightedFaceSet::index<ByDelay>::type WeightedFaceSetByDelay;
  typedef WeightedFaceSet::index<ByFaceId>::type WeightedFaceSetByFaceId;

  unique_ptr<WeightedFaceSet> weightedFaces;
};


const Name CloserSiteStrategy::STRATEGY_NAME("ndn:/localhost/nfd/strategy/closer-site");
//NFD_REGISTER_STRATEGY(CloserSiteStrategy);

CloserSiteStrategy::CloserSiteStrategy(Forwarder& forwarder, const Name& name)
  : Strategy(forwarder, name)
{
}

MyMeasurementInfo*
CloserSiteStrategy::myGetOrCreateMyMeasurementInfo(const fib::Entry& entry)
{
  //BOOST_ASSERT(entry != nullptr);

  //this could return null?
  auto measurementsEntry = getMeasurements().get(entry);

  BOOST_ASSERT(measurementsEntry != nullptr);

  auto measurementsEntryInfo = measurementsEntry->getStrategyInfo<MyMeasurementInfo>();

  if (measurementsEntryInfo == nullptr)
    {
      //measurementsEntryInfo = make_shared<MyMeasurementInfo>();
      //measurementsEntry->setStrategyInfo(measurementsEntryInfo);
      measurementsEntryInfo = measurementsEntry->insertStrategyInfo<MyMeasurementInfo>().first;
    }

  return measurementsEntryInfo;
}

MyPitInfo*
CloserSiteStrategy::myGetOrCreateMyPitInfo(const shared_ptr<pit::Entry>& entry)
{
  // this is the point
  auto pitEntryInfo = entry->getStrategyInfo<MyPitInfo>();

  if (pitEntryInfo == NULL)
    {
      //pitEntryInfo = make_shared<MyPitInfo>();
      pitEntryInfo = entry->insertStrategyInfo<MyPitInfo>().first;
      //entry->setStrategyInfo(pitEntryInfo);
    }

  return pitEntryInfo;
}

void
CloserSiteStrategy::afterReceiveInterest(const Face& inFace, const Interest& interest,
                                        const shared_ptr<pit::Entry>& pitEntry)
{
  // create timer information and attach to PIT entry
  auto pitEntryInfo = myGetOrCreateMyPitInfo(pitEntry);
  const fib::Entry& fibEntry = this->lookupFib(*pitEntry);
  NFD_LOG_TRACE("fibtry " << fibEntry.getPrefix());
  const fib::NextHopList& nexthops = fibEntry.getNextHops();

  // if the Face has no weight, multicast, otherwise stick to one face
  auto measurementsEntryInfo = myGetOrCreateMyMeasurementInfo(fibEntry);

  // reconcile differences between incoming nexthops and those stored
  // on our custom measurement entry info
  uint32_t id = measurementsEntryInfo->updateStoredNextHops(fibEntry.getNextHops());

  if (id != 0){
    for (fib::NextHopList::const_iterator it = nexthops.begin(); it != nexthops.end(); ++it) {
      Face& outFace = it->getFace();
      if (id != outFace.getId()) {
        continue;
      }
      NFD_LOG_TRACE("outFace id " << id);
      if (!wouldViolateScope(inFace, interest, outFace) &&
          canForwardToLegacy(*pitEntry, outFace)) {
        this->sendInterest(pitEntry, outFace, interest);
      }
    }

    if (!hasPendingOutRecords(*pitEntry)) {
      this->rejectPendingInterest(pitEntry);
    }

  }
  else { 
    NFD_LOG_TRACE("id = 0, using multicasting");

    for (fib::NextHopList::const_iterator it = nexthops.begin(); it != nexthops.end(); ++it) {
      Face& outFace = it->getFace();
      NFD_LOG_TRACE("outFace id " << outFace.getId());
      if (!wouldViolateScope(inFace, interest, outFace) &&
          canForwardToLegacy(*pitEntry, outFace)) {
        this->sendInterest(pitEntry, outFace, interest);
      }
    }

    if (!hasPendingOutRecords(*pitEntry)) {
      this->rejectPendingInterest(pitEntry);
    }
  }
}

void
CloserSiteStrategy::beforeSatisfyInterest(const shared_ptr<pit::Entry>& pitEntry,
                                          const Face& inFace,
                                          const Data& data)
{
  NFD_LOG_TRACE("Received Data: " << data.getName() << " from Face id " << inFace.getId());
  auto pitInfo = pitEntry->getStrategyInfo<MyPitInfo>();

  // No start time available, cannot compute delay for this retrieval
  if (pitInfo == nullptr)
    {
      NFD_LOG_TRACE("No start time available for Data " << data.getName());
      return;
    }

  auto& accessor = getMeasurements();

  // Update Face delay measurements and entry lifetimes owned
  // by this strategy while walking up the NameTree
  auto measurementsEntry = accessor.get(*pitEntry);
  if (measurementsEntry != nullptr)
    {
      NFD_LOG_TRACE("accessor returned measurements entry " << measurementsEntry->getName()
                   << " for " << pitEntry->getName());
    }
  else
    {
      NFD_LOG_WARN ("accessor returned invalid measurements entry for " << pitEntry->getName());
    }

  while (measurementsEntry != nullptr)
    {
      auto measurementsEntryInfo = measurementsEntry->getStrategyInfo<MyMeasurementInfo>();

      if (measurementsEntryInfo != nullptr)
        {
if (measurementsEntryInfo->isFaceUpdated(inFace) == true)
    break;

          const milliseconds delay =
            duration_cast<milliseconds>(system_clock::now() - pitInfo->creationTime);

  NFD_LOG_TRACE("Face Id " << inFace.getId() << " = Computed delay of: " << system_clock::now() << " - " << pitInfo->creationTime << " = " << delay);

          accessor.extendLifetime(*measurementsEntry, seconds(16));
          measurementsEntryInfo->updateFaceDelay(inFace, delay);
        }

      measurementsEntry = accessor.getParent(*measurementsEntry);
    }
}

///////////////////////////////////////
// MyMeasurementInfo Implementations //
///////////////////////////////////////
bool
MyMeasurementInfo::isFaceUpdated(const Face& face)
{
  auto& facesById = weightedFaces->get<MyMeasurementInfo::ByFaceId>();
  auto faceEntry = facesById.find(face.getId());
  if (faceEntry != facesById.end())
    {
      return faceEntry->updated;
    }
  return false;
} 

void
MyMeasurementInfo::updateFaceDelay(const Face& face, const milliseconds& delay)
{
  auto& facesById = weightedFaces->get<MyMeasurementInfo::ByFaceId>();
  auto faceEntry = facesById.find(face.getId());

  if (faceEntry != facesById.end())
    {
      if (faceEntry->updated == true) {
        NFD_LOG_DEBUG ("Face " << face.getId() << " has been updated, PASS");
        return;
      }
      auto oldWeight = faceEntry->weight;
      auto result = facesById.modify(faceEntry,
                                     bind(&WeightedFace::modifyWeightedFaceDelay,
                                          _1,
                                          boost::cref(delay)));

      NFD_LOG_DEBUG("updated weight: " << oldWeight << " -> " << faceEntry->weight
                    << " modify: " << result << "\n");
      
      result = facesById.modify(faceEntry,
                                bind(&WeightedFace::modifyWeightedFaceFlag,
                                          _1));
    }
}

uint32_t
MyMeasurementInfo::updateStoredNextHops(const fib::NextHopList& nexthops)
{
  auto updatedFaceSet = new MyMeasurementInfo::WeightedFaceSet;
  auto& facesById = weightedFaces->get<MyMeasurementInfo::ByFaceId>();
  auto& updatedFacesById = updatedFaceSet->get<MyMeasurementInfo::ByFaceId>();

  uint32_t faceId = 0;
  ndn::time::milliseconds delay = ndn::time::milliseconds(99999999999);
  for (auto& hop : nexthops)
    {
      //BOOST_ASSERT(hop.getFace() != nullptr);
      auto& face = hop.getFace();


  bool flag = isFaceUpdated(face);
  if (flag == true) {
    auto faceEntry = facesById.find(face.getId());
    if (faceEntry != facesById.end())
    {
      //NFD_LOG_DEBUG("Face Id " << face.getId() << " delay = " << faceEntry->lastDelay);
      if (delay > faceEntry->lastDelay) {
        delay = faceEntry->lastDelay;
        faceId = face.getId();
      }
    }
  }

      auto weightedIt = facesById.find(face.getId());
      if (weightedIt == facesById.end())
        {
          updatedFacesById.insert(WeightedFace(face));
        }
      else
        {
          updatedFacesById.insert(*weightedIt);
        }
    }

  weightedFaces.reset(updatedFaceSet);
  return faceId;
}


} // namespace fw
} // namespace nfd
