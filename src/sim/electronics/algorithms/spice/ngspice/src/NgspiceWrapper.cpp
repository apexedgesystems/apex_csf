/**
 * @file NgspiceWrapper.cpp
 * @brief Implementation of the libngspice wrapper.
 */

#include "src/sim/electronics/algorithms/spice/ngspice/inc/NgspiceWrapper.hpp"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#if APEX_HAS_LIBNGSPICE
#include <ngspice/sharedspice.h>
#endif

namespace sim::electronics::algorithms::spice::ngspice {

/* ----------------------------- NgspiceStatus ----------------------------- */

const char* toString(NgspiceStatus status) noexcept {
  switch (status) {
  case NgspiceStatus::OK:
    return "OK";
  case NgspiceStatus::ERROR_NOT_INITIALIZED:
    return "ERROR_NOT_INITIALIZED";
  case NgspiceStatus::ERROR_NETLIST_LOAD_FAILED:
    return "ERROR_NETLIST_LOAD_FAILED";
  case NgspiceStatus::ERROR_SIMULATION_FAILED:
    return "ERROR_SIMULATION_FAILED";
  case NgspiceStatus::ERROR_NODE_NOT_FOUND:
    return "ERROR_NODE_NOT_FOUND";
  case NgspiceStatus::ERROR_LIBNGSPICE_NOT_AVAILABLE:
    return "ERROR_LIBNGSPICE_NOT_AVAILABLE";
  }
  return "UNKNOWN";
}

/* ----------------------------- File Helpers ----------------------------- */

static bool readFileToString(const std::string& path, std::string& content) {
  std::ifstream file(path);
  if (!file.is_open()) {
    return false;
  }

  std::stringstream buffer;
  buffer << file.rdbuf();
  content = buffer.str();
  return true;
}

#if APEX_HAS_LIBNGSPICE

/* ----------------------------- ngspice Callbacks ----------------------------- */

static int ngspiceSendChar(char* /*output*/, int /*id*/, void* /*userData*/) {
  // Suppress ngspice output (set to fprintf for debugging)
  return 0;
}

static int ngspiceSendStat(char* /*status*/, int /*id*/, void* /*userData*/) { return 0; }

static int ngspiceControlledExit(int /*status*/, NG_BOOL /*immediate*/, NG_BOOL /*quitexit*/,
                                 int /*id*/, void* /*userData*/) {
  return 0;
}

static int ngspiceSendData(pvecvaluesall /*data*/, int /*count*/, int /*id*/, void* /*userData*/
) {
  return 0;
}

static int ngspiceSendInitData(pvecinfoall /*data*/, int /*id*/, void* /*userData*/) { return 0; }

static int ngspiceBGThreadRunning(NG_BOOL /*running*/, int /*id*/, void* /*userData*/) { return 0; }

/* ----------------------------- ngspice Result Extraction ----------------------------- */

static void extractNodeVoltages(std::unordered_map<std::string, double>& nodeVoltages) {
  nodeVoltages.clear();

  char* curPlot = ngSpice_CurPlot();
  if (curPlot == nullptr) {
    return;
  }

  char** allVecs = ngSpice_AllVecs(curPlot);
  if (allVecs == nullptr) {
    return;
  }

  std::string plotPrefix = std::string(curPlot) + ".";

  for (int i = 0; allVecs[i] != nullptr; ++i) {
    std::string vecName = plotPrefix + allVecs[i];
    pvector_info info = ngGet_Vec_Info(const_cast<char*>(vecName.c_str()));
    if (info == nullptr || info->v_realdata == nullptr) {
      continue;
    }

    // Store using the short name (without plot prefix)
    std::string shortName = allVecs[i];
    nodeVoltages[shortName] = info->v_realdata[info->v_length - 1];
  }
}

#endif // APEX_HAS_LIBNGSPICE

/* ----------------------------- NgspiceWrapper Methods ----------------------------- */

NgspiceWrapper::NgspiceWrapper() noexcept {
  // initialized_ tracks whether a netlist has been loaded (not just library
  // availability). Stays false until loadNetlist/loadNetlistFromString succeeds.
  initialized_ = false;

#if APEX_HAS_LIBNGSPICE
  ngSpice_Init(ngspiceSendChar, ngspiceSendStat, ngspiceControlledExit, ngspiceSendData,
               ngspiceSendInitData, ngspiceBGThreadRunning, nullptr);
#endif
}

NgspiceWrapper::~NgspiceWrapper() noexcept { clear(); }

/* ----------------------------- Netlist Loading ----------------------------- */

NgspiceStatus NgspiceWrapper::loadNetlist(const std::string& netlistPath) noexcept {
  if (!isLibngspiceAvailable()) {
    return NgspiceStatus::ERROR_LIBNGSPICE_NOT_AVAILABLE;
  }

  std::string content;
  if (!readFileToString(netlistPath, content)) {
    return NgspiceStatus::ERROR_NETLIST_LOAD_FAILED;
  }

  return loadNetlistFromString(content);
}

NgspiceStatus NgspiceWrapper::loadNetlistFromString(const std::string& netlistContent) noexcept {
  if (!isLibngspiceAvailable()) {
    return NgspiceStatus::ERROR_LIBNGSPICE_NOT_AVAILABLE;
  }

#if APEX_HAS_LIBNGSPICE
  // Split netlist string into lines for ngSpice_Circ
  std::vector<std::string> lines;
  std::istringstream stream(netlistContent);
  std::string line;

  while (std::getline(stream, line)) {
    lines.push_back(line);
  }

  // Build char* array (ngSpice_Circ expects null-terminated array)
  std::vector<char*> circArray;
  circArray.reserve(lines.size() + 1);
  for (auto& l : lines) {
    circArray.push_back(l.data());
  }
  circArray.push_back(nullptr);

  int result = ngSpice_Circ(circArray.data());
  if (result != 0) {
    return NgspiceStatus::ERROR_NETLIST_LOAD_FAILED;
  }

  initialized_ = true;
  return NgspiceStatus::OK;
#else
  (void)netlistContent;
  return NgspiceStatus::ERROR_LIBNGSPICE_NOT_AVAILABLE;
#endif
}

/* ----------------------------- Simulation ----------------------------- */

NgspiceStatus NgspiceWrapper::runDcOperatingPoint() noexcept {
  if (!isLibngspiceAvailable()) {
    return NgspiceStatus::ERROR_LIBNGSPICE_NOT_AVAILABLE;
  }

  if (!initialized_) {
    return NgspiceStatus::ERROR_NOT_INITIALIZED;
  }

#if APEX_HAS_LIBNGSPICE
  int result = ngSpice_Command(const_cast<char*>("op"));
  if (result != 0) {
    return NgspiceStatus::ERROR_SIMULATION_FAILED;
  }

  extractNodeVoltages(nodeVoltages_);
  return NgspiceStatus::OK;
#else
  return NgspiceStatus::ERROR_LIBNGSPICE_NOT_AVAILABLE;
#endif
}

NgspiceStatus NgspiceWrapper::runTransient(double tstop, double tstep) noexcept {
  if (!isLibngspiceAvailable()) {
    return NgspiceStatus::ERROR_LIBNGSPICE_NOT_AVAILABLE;
  }

  if (!initialized_) {
    return NgspiceStatus::ERROR_NOT_INITIALIZED;
  }

#if APEX_HAS_LIBNGSPICE
  // Build tran command: "tran <tstep> <tstop>"
  char cmd[128];
  std::snprintf(cmd, sizeof(cmd), "tran %e %e", tstep, tstop);

  int result = ngSpice_Command(cmd);
  if (result != 0) {
    return NgspiceStatus::ERROR_SIMULATION_FAILED;
  }

  // Extract transient results
  nodeWaveforms_.clear();
  timePoints_.clear();

  char* curPlot = ngSpice_CurPlot();
  if (curPlot == nullptr) {
    return NgspiceStatus::ERROR_SIMULATION_FAILED;
  }

  char** allVecs = ngSpice_AllVecs(curPlot);
  if (allVecs == nullptr) {
    return NgspiceStatus::ERROR_SIMULATION_FAILED;
  }

  std::string plotPrefix = std::string(curPlot) + ".";

  // Extract time vector first
  std::string timeVecName = plotPrefix + "time";
  pvector_info timeInfo = ngGet_Vec_Info(const_cast<char*>(timeVecName.c_str()));
  if (timeInfo != nullptr && timeInfo->v_realdata != nullptr) {
    timePoints_.assign(timeInfo->v_realdata, timeInfo->v_realdata + timeInfo->v_length);
  }

  // Extract all other vectors
  for (int i = 0; allVecs[i] != nullptr; ++i) {
    std::string vecName = plotPrefix + allVecs[i];
    pvector_info info = ngGet_Vec_Info(const_cast<char*>(vecName.c_str()));
    if (info == nullptr || info->v_realdata == nullptr) {
      continue;
    }

    std::string shortName = allVecs[i];
    nodeWaveforms_[shortName].assign(info->v_realdata, info->v_realdata + info->v_length);
  }

  // Also populate DC voltages with final time point
  extractNodeVoltages(nodeVoltages_);

  return NgspiceStatus::OK;
#else
  (void)tstop;
  (void)tstep;
  return NgspiceStatus::ERROR_LIBNGSPICE_NOT_AVAILABLE;
#endif
}

/* ----------------------------- Result Extraction ----------------------------- */

NgspiceStatus NgspiceWrapper::getNodeVoltage(const std::string& nodeName,
                                             double& voltage) const noexcept {
  auto it = nodeVoltages_.find(nodeName);
  if (it == nodeVoltages_.end()) {
    return NgspiceStatus::ERROR_NODE_NOT_FOUND;
  }

  voltage = it->second;
  return NgspiceStatus::OK;
}

const std::unordered_map<std::string, double>& NgspiceWrapper::getAllNodeVoltages() const noexcept {
  return nodeVoltages_;
}

NgspiceStatus NgspiceWrapper::getNodeWaveform(const std::string& nodeName,
                                              std::vector<double>& times,
                                              std::vector<double>& voltages) const noexcept {
  auto it = nodeWaveforms_.find(nodeName);
  if (it == nodeWaveforms_.end()) {
    return NgspiceStatus::ERROR_NODE_NOT_FOUND;
  }

  times = timePoints_;
  voltages = it->second;
  return NgspiceStatus::OK;
}

/* ----------------------------- Utilities ----------------------------- */

bool NgspiceWrapper::isLibngspiceAvailable() noexcept {
#if APEX_HAS_LIBNGSPICE
  return true;
#else
  return false;
#endif
}

std::string NgspiceWrapper::getVersion() const noexcept {
#if APEX_HAS_LIBNGSPICE
  return "ngspice-36 (libngspice)";
#else
  return "libngspice not available";
#endif
}

void NgspiceWrapper::clear() noexcept {
  nodeVoltages_.clear();
  nodeWaveforms_.clear();
  timePoints_.clear();
  initialized_ = false;

#if APEX_HAS_LIBNGSPICE
  ngSpice_Command(const_cast<char*>("destroy all"));
#endif
}

} // namespace sim::electronics::algorithms::spice::ngspice
