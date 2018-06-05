#include "GENIEReWeightEngineConfig.hh"
#include "GENIEReWeightParamConfig.hh"

#include "nusimdata/SimulationBase/GTruth.h"
#include "nusimdata/SimulationBase/MCTruth.h"

#include "larsyst/interface/ISystProvider_tool.hh"
#include "larsyst/utility/extend_SystMetaData.hh"
#include "larsyst/utility/printers.hh"
#include "larsyst/utility/string_parsers.hh"

#include "nutools/EventGeneratorBase/GENIE/GENIE2ART.h"

#include "art/Framework/Services/Optional/RandomNumberGenerator.h"
#include "art/Framework/Services/Registry/ServiceHandle.h"
#include "art/Utilities/ToolMacros.h"

#include "EVGCore/EventRecord.h"
#include "Messenger/Messenger.h"

#include "CLHEP/Random/MTwistEngine.h"
#include "CLHEP/Random/RandGaussQ.h"

#include <chrono>
#include <sstream>

using namespace larsyst;
using namespace fhicl;

class GENIEReWeight : public larsyst::ISystProvider_tool {
public:
  explicit GENIEReWeight(ParameterSet const &);

  SystMetaData ConfigureFromFHICL(ParameterSet const &, paramId_t);

  bool Configure();
  std::unique_ptr<EventResponse> GetEventResponse(art::Event &);
  std::string AsString();

private:
  std::unique_ptr<genie::rew::GReWeight> GReWeightEngine;

  std::map<size_t, std::map<genie::rew::GSyst_t, size_t>>
      ResponseToGENIEParameters;
  void extend_ResponseToGENIEParameters(
      std::map<size_t, std::map<genie::rew::GSyst_t, size_t>> &&);
  std::set<genie::rew::GSyst_t> GENIEEngineDials;
  std::string fGENIEModuleLabel = "generator";
};

GENIEReWeight::GENIEReWeight(ParameterSet const &params)
    : ISystProvider_tool(params), GReWeightEngine{nullptr} {}

std::string GENIEReWeight::AsString() {
  CheckHaveMetaData();
  return "";
}

SystMetaData GENIEReWeight::ConfigureFromFHICL(ParameterSet const &params,
                                               paramId_t firstParamId) {

  std::cout << "[INFO]: Configuring GENIEReWeight" << std::endl;

  Table<nusyst::GENIEReWeightParamConfig> cfg{
      params, std::set<std::string>{"tool_type", "uniqueName"}};

  SystMetaData QEmd = nusyst::ConfigureQEParameterHeaders(cfg, firstParamId);
  firstParamId += QEmd.headers.size();
  SystMetaData NCELmd =
      nusyst::ConfigureNCELParameterHeaders(cfg, firstParamId);
  firstParamId += NCELmd.headers.size();
  SystMetaData RESmd = nusyst::ConfigureRESParameterHeaders(cfg, firstParamId);
  firstParamId += RESmd.headers.size();
  SystMetaData COHmd = nusyst::ConfigureCOHParameterHeaders(cfg, firstParamId);
  firstParamId += COHmd.headers.size();
  SystMetaData DISmd = nusyst::ConfigureDISParameterHeaders(cfg, firstParamId);
  firstParamId += DISmd.headers.size();
  SystMetaData FSImd = nusyst::ConfigureFSIParameterHeaders(cfg, firstParamId);
  firstParamId += FSImd.headers.size();
  SystMetaData Othermd =
      nusyst::ConfigureOtherParameterHeaders(cfg, firstParamId);
  firstParamId += Othermd.headers.size();

  // Don't extend inline to make firstParamId incrementing more clear.
  extend_SystMetaData(QEmd, NCELmd);
  extend_SystMetaData(QEmd, RESmd);
  extend_SystMetaData(QEmd, COHmd);
  extend_SystMetaData(QEmd, DISmd);
  extend_SystMetaData(QEmd, FSImd);
  extend_SystMetaData(QEmd, Othermd);

  return QEmd;
}

void GENIEReWeight::extend_ResponseToGENIEParameters(
    std::map<size_t, std::map<genie::rew::GSyst_t, size_t>> &&other) {
  for (auto &&o : other) {
    if (ResponseToGENIEParameters.find(o.first) !=
        ResponseToGENIEParameters.end()) {
      std::cout << "[ERROR]: Attempted to merge GENIE GSyst map, but found "
                   "duplicate response parameter index: "
                << o.first << ", which corresponds to parameter: "
                << std::quoted(fMetaData.headers[o.first].prettyName)
                << std::endl;
      throw;
    }
    ResponseToGENIEParameters.insert(std::move(o));
  }
}

bool GENIEReWeight::Configure() {
  GReWeightEngine = std::make_unique<genie::rew::GReWeight>();
  genie::rew::GSystSet &gdials = GReWeightEngine->Systematics();
  genie::Messenger::Instance()->SetPrioritiesFromXmlFile(
      "Messenger_whisper.xml");

  extend_ResponseToGENIEParameters(
      nusyst::ConfigureQEWeightEngine(fMetaData, GReWeightEngine));
  extend_ResponseToGENIEParameters(
      nusyst::ConfigureNCELWeightEngine(fMetaData, GReWeightEngine));
  extend_ResponseToGENIEParameters(
      nusyst::ConfigureRESWeightEngine(fMetaData, GReWeightEngine));
  extend_ResponseToGENIEParameters(
      nusyst::ConfigureCOHWeightEngine(fMetaData, GReWeightEngine));
  extend_ResponseToGENIEParameters(
      nusyst::ConfigureDISWeightEngine(fMetaData, GReWeightEngine));
  extend_ResponseToGENIEParameters(
      nusyst::ConfigureFSIWeightEngine(fMetaData, GReWeightEngine));
  extend_ResponseToGENIEParameters(
      nusyst::ConfigureOtherWeightEngine(fMetaData, GReWeightEngine));

  std::cout << "[INFO]: NuSyst provider "
            << std::quoted(GetFullyQualifiedName()) << " configured with "
            << ResponseToGENIEParameters.size() << " response parameters. "
            << std::endl;

  for (auto const &resp : ResponseToGENIEParameters) {
    std::cout << "\tParameter #" << resp.first << ":"
              << std::quoted(fMetaData.headers[resp.first].prettyName)
              << " uses " << resp.second.size()
              << " GENIE parameters: " << std::endl;
    for (auto const &gparpair : resp.second) {
      std::cout << "\t\t" << genie::rew::GSyst::AsString(gparpair.first) << ", "
                << fMetaData.headers[gparpair.second].systParamId << ":"
                << std::quoted(fMetaData.headers[gparpair.second].prettyName)
                << std::endl;
      gdials.Init(gparpair.first);
      GENIEEngineDials.insert(gparpair.first);
    }
  }
  return true;
}

std::unique_ptr<EventResponse> GENIEReWeight::GetEventResponse(art::Event &e) {
  std::unique_ptr<EventResponse> er = std::make_unique<EventResponse>();

  art::Handle<std::vector<simb::MCTruth>> mcTruthHandle;
  art::Handle<std::vector<simb::GTruth>> gTruthHandle;
  e.getByLabel(fGENIEModuleLabel, mcTruthHandle);
  e.getByLabel(fGENIEModuleLabel, gTruthHandle);

  size_t NEventUnits = mcTruthHandle->size();
  if (mcTruthHandle->size() != gTruthHandle->size()) {
    std::cout << "[WARN]: Found " << mcTruthHandle->size()
              << " MC truth instances, and " << gTruthHandle->size()
              << " GENIE truth instances in event " << e.event() << std::endl;
    NEventUnits = std::min(mcTruthHandle->size(), gTruthHandle->size());
  }

  std::vector<std::unique_ptr<genie::EventRecord>> gheps;
  for (size_t eu_it = 0; eu_it < NEventUnits; ++eu_it) {
    gheps.emplace_back(
        evgb::RetrieveGHEP(mcTruthHandle->at(eu_it), gTruthHandle->at(eu_it)));
    std::cout << "[INFO]: GENIE Interaction: "
              << gheps.back()->Summary()->AsString() << std::endl;
  }

  er->responses.resize(NEventUnits);

  // Calculate response for each dial-set
  for (auto const &resp : ResponseToGENIEParameters) {

    paramId_t respParamId = fMetaData.headers[resp.first].systParamId;
    // Reset all dials
    for (auto const &gpar : GENIEEngineDials) {
      GReWeightEngine->Systematics().Set(gpar, 0);
    }

    if (fMetaData.headers[resp.first].isCorrection) {

      for (auto const &gparpair : resp.second) {
        GReWeightEngine->Systematics().Set(
            gparpair.first,
            fMetaData.headers[gparpair.second].centralParamValue);
      }
      std::cout << "[INFO]: GDials: " << std::endl;
      for (auto const &gpar : GENIEEngineDials) {
        std::cout << "\t" << genie::rew::GSyst::AsString(gpar) << " = "
                  << GReWeightEngine->Systematics().Info(gpar)->CurValue
                  << std::endl;
      }

      auto preconfigure = std::chrono::high_resolution_clock::now();
      GReWeightEngine->Reconfigure();
      auto postconfigure = std::chrono::high_resolution_clock::now();
      auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
          postconfigure - preconfigure);
      std::cout << "Reconfigure took " << milliseconds.count() << " ms"
                << std::endl;
      for (size_t eu_it = 0; eu_it < NEventUnits; ++eu_it) {
        er->responses[eu_it][respParamId].push_back(
            GReWeightEngine->CalcWeight(*gheps[eu_it]));
      }

    } else {
      size_t NVariations = fMetaData.headers[resp.first].paramVariations.size();
      for (size_t var_it = 0; var_it < NVariations; ++var_it) {
        // Calculate weights for each.
        for (auto const &gparpair : resp.second) {
          GReWeightEngine->Systematics().Set(
              gparpair.first,
              fMetaData.headers[gparpair.second].paramVariations[var_it]);
        }
        if (var_it == 0) {
          std::cout << "[INFO]: Event " << e.event() << ", ParamSet "
                    << std::quoted(fMetaData.headers[resp.first].prettyName)
                    << std::endl;
          std::cout << "[INFO]: GDials: " << std::endl;
          for (auto const &gpar : GENIEEngineDials) {
            std::cout << "\t" << genie::rew::GSyst::AsString(gpar) << " = "
                      << GReWeightEngine->Systematics().Info(gpar)->CurValue
                      << std::endl;
          }
        }

        auto preconfigure = std::chrono::high_resolution_clock::now();
        GReWeightEngine->Reconfigure();
        auto postconfigure = std::chrono::high_resolution_clock::now();
        auto milliseconds =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                postconfigure - preconfigure);
        std::cout << "Event " << e.event() << ", ParamSet "
                  << std::quoted(fMetaData.headers[resp.first].prettyName)
                  << "Reconfigure " << var_it << ", took "
                  << milliseconds.count() << " ms" << std::endl;

        for (size_t eu_it = 0; eu_it < NEventUnits; ++eu_it) {
          auto preweight = std::chrono::high_resolution_clock::now();
          er->responses[eu_it][respParamId].push_back(
              GReWeightEngine->CalcWeight(*gheps[eu_it]));
          auto postweight = std::chrono::high_resolution_clock::now();
          auto milliseconds =
              std::chrono::duration_cast<std::chrono::milliseconds>(postweight -
                                                                    preweight);
          std::cout << "\tReweight[" << eu_it << "](" << e.event() << ") took "
                    << milliseconds.count() << " ms, got "
                    << er->responses[eu_it][respParamId].back() << std::endl;
        }
      }
    }
  }
  return er;
}

DEFINE_ART_CLASS_TOOL(GENIEReWeight)