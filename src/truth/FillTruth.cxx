/// \file FillTruth.cxx
///
/// Fill truth branches.
///
/// \author  J. Wolcott <jwolcott@fnal.gov>, based on code by C. Marshall <chris.marshall@rochester.edu>
/// \date    Jan. 2022

#include "FillTruth.h"

// ROOT
#include "TLorentzVector.h"
#include "TVector3.h"
#include "TTree.h"

// GENIE
#include "Framework/EventGen/EventRecord.h"
#include "Framework/Ntuple/NtpMCEventRecord.h"
#include "Framework/GHEP/GHepParticle.h"

// Standard Record format
#include "duneanaobj/StandardRecord/StandardRecord.h"

// ND_CAFMaker
#include "CAF.h"
#include "Params.h"

/// duneanaobj not guaranteed to be the same as GENIE scattering types
caf::ScatteringMode GENIE2CAF(genie::EScatteringType sc)
{
  switch(sc)
  {
    case genie::kScQuasiElastic:
      return caf::kQE;

    case genie::kScSingleKaon:
      return caf::kSingleKaon;

    case genie::kScDeepInelastic:
      return caf::kDIS;

    case genie::kScResonant:
      return caf::kRes;

    case genie::kScCoherentProduction:
      return caf::kCoh;

    case genie::kScDiffractive:
      return caf::kDiffractive;

    case genie::kScNuElectronElastic:
      return caf::kNuElectronElastic;

    case genie::kScInverseMuDecay:
      return caf::kInvMuonDecay;

    case genie::kScAMNuGamma:
      return caf::kAMNuGamma;

    case genie::kScMEC:
      return caf::kMEC;

    case genie::kScCoherentElastic:
      return caf::kCohElastic;

    case genie::kScInverseBetaDecay:
      return caf::kInverseBetaDecay;

    case genie::kScGlashowResonance:
      return caf::kGlashowResonance;

    case genie::kScIMDAnnihilation:
      return caf::kIMDAnnihilation;

    case genie::kScPhotonCoherent:
      return caf::kPhotonCoh;

    case genie::kScPhotonResonance:
      return caf::kPhotonRes;

    case genie::kScDarkMatterElastic:
      return caf::kDarkMatterElastic;

    case genie::kScDarkMatterDeepInelastic:
      return caf::kDarkMatterDIS;

    case genie::kScDarkMatterElectron:
      return caf::kDarkMatterElectron;

    case genie::kScUnknown: [[fallthrough]];
    case genie::kScNull:
      return caf::kUnknownMode;

    default:
      std::cerr << "Unrecognized GENIE scattering mode: " << sc << "\n";
      abort();
  }

}

namespace cafmaker
{

  // ------------------------------------------------------------
  TruthMatcher::TruthMatcher(TTree *gTree, const genie::NtpMCEventRecord *gEvt)
    : fGTree(gTree), fGEvt(gEvt)
  {}

  // --------------------------------------------------------------
  void TruthMatcher::FillInteraction(caf::SRTrueInteraction& nu, const genie::NtpMCEventRecord * gEvt)
  {

    genie::EventRecord * event = gEvt->event;
    genie::Interaction * in = event->Summary();

    nu.id = gEvt->hdr.ievent;  //todo: need to make sure this ID is the one we get all the way out the other end from det sim

    TLorentzVector vtx = *(event->Vertex());
    nu.vtx = vtx.Vect();
    // nu.isvtxcont =    // todo: how do we get the right geometry info here?
    nu.time = vtx.T();

    nu.pdg = in->InitState().ProbePdg();
    nu.pdgorig = in->InitState().ProbePdg(); // fill this for similarity with FD, but no oscillations

    nu.iscc = in->ProcInfo().IsWeakCC();
    nu.mode = GENIE2CAF(in->ProcInfo().ScatteringTypeId());
    nu.targetPDG = in->InitState().Tgt().Pdg();
    nu.hitnuc = in->InitState().Tgt().HitNucPdg();

    //todo: get this from Hugh G or somebody who will get it right
    // nu.removalE =

    TLorentzVector lepP4;
    TLorentzVector nuP4nuc = *(in->InitState().GetProbeP4(genie::kRfHitNucRest));
    TLorentzVector nuP4 = *(in->InitState().GetProbeP4(genie::kRfLab));
    nu.E = in->InitState().ProbeE(genie::kRfLab);
    nu.momentum = nuP4.Vect();

    // true 4-momentum transfer
    TLorentzVector q = nuP4-lepP4;

    // Q2, W, x, y frequently do not get filled in GENIE Kinematics object, so calculate manually
    const double Mnuc = 0.939;  // average nucleon mass
    nu.Q2 = -q.Mag2();
    nu.q0 = q.E();
    nu.modq = q.Vect().Mag();
    nu.W = sqrt(Mnuc*Mnuc + 2.*nu.q0*Mnuc + q.Mag2()); // "Wexp"
    nu.bjorkenX = nu.Q2/(2*Mnuc*nu.q0);
    nu.inelasticity = nu.q0/nu.E;
    if (nu.mode == caf::kCoh || nu.mode == caf::kDiffractive)
      nu.t = in->Kine().t();

    nu.ischarm = in->ExclTag().IsCharmEvent();
    nu.isseaquark = in->ProcInfo().IsDeepInelastic() && in->InitState().Tgt().HitSeaQrk();
    if (nu.mode == caf::kRes)
      nu.resnum = static_cast<int>(in->ExclTag().Resonance());

    nu.xsec = event->XSec();
    nu.genweight = event->Weight();

    // loop truth particles

    int stableCtr = 0;
    for (int j=0; j< event->GetEntries(); j++)
    {
      auto p = dynamic_cast<const genie::GHepParticle *>((*event)[j]);
      if( p->Status() != genie::EGHepStatus::kIStStableFinalState
          && p->Status() != genie::EGHepStatus::kIStHadronInTheNucleus) continue;

      caf::SRTrueParticle part;
      part.pdg = p->Pdg();
      part.interaction_id = nu.id;
      part.time = nu.time;

      part.p = *p->P4();
      part.start_pos = p->X4()->Vect();

      // remaining fields need to be filled in with post-G4 info

      if( p->Status() != genie::EGHepStatus::kIStStableFinalState)
      {
        part.G4ID = stableCtr++;        // todo: check if this is always the number given to G4!
        nu.prim.push_back(std::move(part));

        if( p->Pdg() == 2212 ) nu.nproton++;
        else if( p->Pdg() == 2112 ) nu.nneutron++;
        else if( p->Pdg() ==  211 ) nu.npip++;
        else if( p->Pdg() == -211 ) nu.npim++;
        else if( p->Pdg() ==  111 ) nu.npi0++;
      }
      else // kIStHadronInTheNucleus
        nu.prefsi.push_back(std::move(part));

    }

    // todo: need to fill the flux variables in.  for 2x2, info should come from a genie::flux::GNuMIFluxPassThroughInfo object created by the flux driver.
    //       for DUNE beam, I assume there's an analogous thing?
    //nu.baseline =
  //  nu.prod_vtx       = ;              ///< Neutrino production vertex [cm; beam coordinates]
  //  nu.parent_dcy_mom =  ;        ///< Neutrino parent momentum at decay [GeV; beam coordinates]
  //  nu.parent_dcy_mode = ;  ///< Parent hadron/muon decay mode
  //  nu.parent_pdg      = ;   ///< PDG Code of parent particle ID
  //  nu.parent_dcy_E    = ; ///< Neutrino parent energy at decay [GeV]
  //  nu.imp_weight      = ; ///< Importance weight from flux file


    // Add DUNErw weights to the CAF
    nu.xsec_cvwgt = 1;

    // fixme: the following is disabled until DIRT-II finishes on model + uncertainty decisions
    //systtools::event_unit_response_w_cv_t resp = rh.GetEventVariationAndCVResponse(*event);
    //for( const systtools::VarAndCVResponse& it : resp ) {
    //  // Need begin/end to convert double to float
    //  sr.xsSyst_wgt.emplace_back(it.responses.begin(), it.responses.end());
    //  sr.cvwgt.push_back(it.CV_response);
    //  sr.total_xsSyst_cv_wgt *= it.CV_response;
    //}
  }

  // ------------------------------------------------------------
  caf::SRTrueParticle &
  TruthMatcher::GetTrueParticle(caf::StandardRecord &sr, int ixnID, int G4ID, bool isPrimary, bool createNew) const
  {
    caf::SRTrueInteraction & ixn = GetTrueInteraction(sr, ixnID, createNew);

    caf::SRTrueParticle * part = nullptr;
    std::vector<caf::SRTrueParticle> & collection = (isPrimary) ? ixn.prim : ixn.sec;
    int & counter = (isPrimary) ? ixn.nprim : ixn.nsec;
    if ( auto itPart = std::find_if(collection.begin(), collection.end(), [G4ID](const caf::SRTrueParticle & part) { return part.G4ID == G4ID; });
         itPart == collection.end() )
    {
      if (!createNew)
        throw std::runtime_error("True particle with interaction ID " + std::to_string(ixnID) + " and G4ID " + std::to_string(G4ID)
                                 + " was not found in the " + std::string(isPrimary ? "primary" : "secondary") + " true particle collection");

      collection.emplace_back();
      counter++;

      part = &collection.back();
      part->G4ID = G4ID;
      part->interaction_id = ixnID;
    }
    else
      part = &(*itPart);

    return *part;
  }

  // ------------------------------------------------------------
  caf::SRTrueInteraction & TruthMatcher::GetTrueInteraction(caf::StandardRecord &sr, int ixnID, bool createNew) const
  {
    caf::SRTrueInteraction * ixn = nullptr;

    // if we can't find a GENIE record with matching ID, we may need to make a new one
    if ( auto itIxn = std::find_if(sr.mc.nu.begin(), sr.mc.nu.end(), [ixnID](const caf::SRTrueInteraction & ixn) { return ixn.id == ixnID; });
         itIxn == sr.mc.nu.end() )
    {
      if (!createNew)
        throw std::runtime_error("True interaction with interaction ID " + std::to_string(ixnID) + " was not found in this StandardRecord");

      if (fGTree->GetReadEvent() < 0)
        fGTree->GetEntry(0);

      // the most likely place to find the matching event is just beyond wherever we currently are,
      // so look there first, then loop back around to consider events previous to where we were
      for (int evtIdx = fGTree->GetReadEvent(); evtIdx % fGTree->GetEntries() != fGTree->GetReadEvent(); evtIdx++)
      {
        fGTree->GetEntry(evtIdx % fGTree->GetEntries());

        if (fGEvt->hdr.ievent == ixnID)
          break;
      }

      if (fGEvt->hdr.ievent != ixnID)
        throw std::runtime_error("Could not locate GENIE event record with ID = " + std::to_string(ixnID));

      sr.mc.nu.emplace_back();
      sr.mc.nnu++;

      ixn = &sr.mc.nu.back();
      FillInteraction(*ixn, fGEvt);
    }
    else
      ixn = &(*itIxn);

    return *ixn;
  }

}
