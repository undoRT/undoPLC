/**
 * @file undoSystem.cpp
 * @brief Source file for the basic undoSystem
 * @author Salvatore Bamundo
 * @date June 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#include "undoSystem.hpp"
#include <fstream>
#include <sstream>
#include <boost/algorithm/string.hpp>
#include <boost/sort/spreadsort/integer_sort.hpp>

// =============== Public methods ===============

/**
 * @class UndoSys
 * @brief Manages low-level system configurations for real-time operations.
 */

/**
  * @brief Construct a new Undo Sys:: Undo Sys object
  */
UndoSys::UndoSys()
{
   // Auto-discover TSC frequency during system initiaization
   initTscFrequency();
}

/**
 * @brief Configures specific CPU cores to run at their nominal frequency using the performance governor.
 * @param cores Vector of CPU core IDs to be configured.
 * @return true if configuration succeeded for all specified cores, false otherwise.
 */
bool UndoSys::setCpuNominalFrequency(const std::vector<int>& cores)
{
   UndoLog& logger = UndoLog::getInstance();
   bool ret = true;

   for (int coreId : cores) {
      std::string basePath = "/sys/devices/system/cpu/cpu" + std::to_string(coreId) + "/cpufreq/";

      // 1. Read the hardware nominal base frequency
      std::ifstream baseFreqFile(basePath + "base_frequency");
      std::string baseFreqVal;

      if (!baseFreqFile.is_open() || !(baseFreqFile >> baseFreqVal)) {
         logger.logRT(LogDomain::PLC, LOG_ERR, "UndoSys: Cannot read base_frequency for CPU %u", coreId);
         ret = false;
         continue;
      }
      baseFreqFile.close();

      // 2. Set governor to performance to ensure deterministic wake-up times
      if (!writeSysfsAttribute(basePath + "scaling_governor", "performance")) {
         logger.logRT(LogDomain::PLC, LOG_ERR, "UndoSys: Failed to set performance governor on CPU %u", coreId);
         ret = false;
         continue;
      }

      // 3. Set max scaling frequency to nominal base frequency to clamp Turbo Boost
      if (!writeSysfsAttribute(basePath + "scaling_max_freq", baseFreqVal)) {
         logger.logRT(LogDomain::PLC, LOG_ERR, "UndoSys: Failed to clamp Turbo Boost via scaling_max_freq on CPU %u", coreId);
         ret = false;
         continue;
      }

      logger.logRT(LogDomain::PLC,
                   LOG_INFO,
                   "UndoSys: CPU %u successfully locked to nominal frequency (%f GHz) with performance governor.",
                   coreId,
                   (std::stod(baseFreqVal) / 1000000.0));
   }

   return ret;
}

/**
 * @brief Restores specific CPU cores to their default powersave governor and unlocks maximum hardware frequency.
 * @param cores Vector of CPU core IDs to be reset.
 * @return true if restoration succeeded for all specified cores, false otherwise.
 */
bool UndoSys::resetCpuFrequency(const std::vector<int>& cores)
{
   UndoLog& logger = UndoLog::getInstance();
   bool ret = true;

   for (int coreId : cores) {
      std::string basePath = "/sys/devices/system/cpu/cpu" + std::to_string(coreId) + "/cpufreq/";

      // Read the maximum absolute hardware frequency supported by the silicon
      std::ifstream maxFreqFile(basePath + "cpuinfo_max_freq");
      std::string maxFreqVal;

      if (!maxFreqFile.is_open() || !(maxFreqFile >> maxFreqVal)) {
         logger.logRT(LogDomain::PLC, LOG_ERR, "UndoSys: Cannot read cpuinfo_max_freq for CPU %u", coreId);
         ret = false;
         continue;
      }
      maxFreqFile.close();

      // Restore maximum hardware scaling range to allow standard behavior/Turbo Boost
      if (!writeSysfsAttribute(basePath + "scaling_max_freq", maxFreqVal)) {
         logger.logRT(LogDomain::PLC, LOG_ERR, "UndoSys: Failed to restore scaling_max_freq on CPU %u", coreId);
         ret = false;
         continue;
      }

      // Set governor back to powersave for standard OS power management
      if (!writeSysfsAttribute(basePath + "scaling_governor", "powersave")) {
         logger.logRT(LogDomain::PLC, LOG_ERR, "UndoSys: Failed to restore powersave governor on CPU %u", coreId);
         ret = false;
         continue;
      }

      logger.logRT(LogDomain::PLC, LOG_INFO, "UndoSys: CPU %u successfully restored to powersave governor.", coreId);
   }

   return ret;
}

/**
 * @brief Initializes the TSC frequency by checking CPUID or sysfs fallback.
 * @return true if frequency was successfully discovered, false otherwise.
 */
bool UndoSys::initTscFrequency()
{
   if (_tscFrequencyHz) {
      return true;
   }

   unsigned int eax = 0, ebx = 0, ecx = 0, edx = 0;
   // Try discovering via native Intel CPUID leaf 0x15
   if (__get_cpuid(0x15, &eax, &ebx, &ecx, &edx) && eax != 0 && ebx != 0 && ecx != 0) {
      _tscFrequencyHz = static_cast<uint64_t>(ecx) * ebx / eax;
      return true;
   }

   // Fallback to sysfs if CPUID leaf 0x15 is unpopulated or untrusted
   std::string sysfsPath = "/sys/devices/system/cpu/cpu0/tsc_freq_khz";
   std::ifstream tscFile(sysfsPath);
   uint64_t freqKhz = 0;

   if (tscFile.is_open() && (tscFile >> freqKhz)) {
      _tscFrequencyHz = freqKhz * 1000ULL;
      tscFile.close();
      return true;
   }

   if (tscFile.is_open()) {
      tscFile.close();
   }

   _tscFrequencyHz = 0;
   return false;
}

/**
 * @brief Explicitly logs the resolved TSC frequency once the logging infrastructure is safe.
 */
void UndoSys::logSystemStatus()
{
   UndoLog& logger = UndoLog::getInstance();
   if (_tscFrequencyHz > 0) {
      logger.logRT(LogDomain::PLC, LOG_INFO, "UndoSys: TSC frequency validated at: %f MHz", (_tscFrequencyHz / 1000000.0));
   } else {
      logger.logRT(LogDomain::PLC, LOG_ERR, "UndoSys: Critical: Failed to resolve Invariant TSC frequency.");
   }
}

/**
 * @brief Converts a duration in TSC clock cycles to nanoseconds based on the discovered CPU frequency.
 * @param tscCycles The delta or absolute number of TSC cycles.
 * @return The converted duration in nanoseconds. Returns 0 if the TSC frequency was not initialized.
 */
uint64_t UndoSys::tsc2Ns(uint64_t tscCycles) const
{
   if (_tscFrequencyHz == 0) [[unlikely]] {
      return 0;
   }

   return (tscCycles * 1000000ULL) / (_tscFrequencyHz / 1000ULL);
}

/**
 * @brief Get a vector with all the isolated cpus in order
 * 
 * @return vector of isolated cpus
 */
const std::vector<int>& UndoSys::getIsolatedCpu()
{
   if (_isolatedCoresChecked) {
      return _isolatedCores;
   }

   // Path to the isolated CPUs sysfs file
   std::string filePath = "/sys/devices/system/cpu/isolated";

   // Read the set of isolated CPU(s)
   std::ifstream isolatedCpusFile(filePath);
   std::string isolatedCpus;

   if (!isolatedCpusFile.is_open()) {
      UndoLog& logger = UndoLog::getInstance();
      logger.logRT(LogDomain::PLC, LOG_ERR, "UndoSys: Cannot open isolated CPUs file: %s", filePath.data());
      return _isolatedCores;
   }

   if (!(isolatedCpusFile >> isolatedCpus)) {
      // File might be empty if no cores are isolated, which is a valid state
      isolatedCpusFile.close();
      _isolatedCoresChecked = true;
      return _isolatedCores;
   }

   isolatedCpusFile.close();

   if (isolatedCpus.empty()) {
      _isolatedCoresChecked = true;
      return _isolatedCores;
   }

   std::vector<std::string> vecIsolatedCpus;
   boost::split(vecIsolatedCpus, isolatedCpus, boost::is_any_of(","));

   for (const auto& grpCpu : vecIsolatedCpus) {
      if (grpCpu.find("-") == std::string::npos) {
         // Single CPU item
         _isolatedCores.push_back(std::stoi(grpCpu));
         continue;
      }

      // Handle CPU range group (e.g., "10-11")
      std::vector<std::string> vecGrpCpus;
      boost::split(vecGrpCpus, grpCpu, boost::is_any_of("-"));

      if (vecGrpCpus.size() != 2) {
         UndoLog& logger = UndoLog::getInstance();
         logger.logRT(LogDomain::PLC, LOG_ERR, "UndoSys: Invalid group range format for isolated CPUs: %s", grpCpu.data());
         _isolatedCores.clear(); // Clear to avoid partial invalid states
         return _isolatedCores;
      }

      int start = std::stoi(vecGrpCpus[0]);
      int end = std::stoi(vecGrpCpus[1]);

      for (int i = start; i <= end; ++i) {
         _isolatedCores.push_back(i);
      }
   }

   // Sort the isolated cores in descending order as requested
   using namespace boost::sort::spreadsort;
   integer_sort(_isolatedCores.begin(), _isolatedCores.end(), [](int x, int y) { return x < y; });

   _isolatedCoresChecked = true;
   return _isolatedCores;
}

/**
 * @brief Get the total number of the cpus
 * 
 * @return number of cpus, -1 if error
 */
int UndoSys::getTotalCpu()
{
   if (_totNumCores != -1) {
      return _totNumCores;
   }

   std::string filePath = "/sys/devices/system/cpu/online";
   std::ifstream onlineCpuFile(filePath);
   std::string totNumCpusStr;

   if (!onlineCpuFile.is_open()) {
      UndoLog& logger = UndoLog::getInstance();
      logger.logRT(LogDomain::PLC, LOG_ERR, "UndoSys: Cannot open online CPUs file: %s", filePath.data());
      _totNumCores = -1;
      return _totNumCores;
   }

   if (!(onlineCpuFile >> totNumCpusStr)) {
      onlineCpuFile.close();
      _totNumCores = -1;
      return _totNumCores;
   }
   onlineCpuFile.close();

   std::vector<std::string> vecCpus;
   boost::split(vecCpus, totNumCpusStr, boost::is_any_of("-"));

   if (vecCpus.size() != 2) {
      UndoLog& logger = UndoLog::getInstance();
      logger.logRT(LogDomain::PLC, LOG_ERR, "UndoSys: Invalid group range format for total CPUs: %s", totNumCpusStr.data());
      _totNumCores = -1;
      return _totNumCores;
   }

   int start = std::stoi(vecCpus[0]);
   int end = std::stoi(vecCpus[1]);

   _totNumCores = (end - start) + 1;

   return _totNumCores;
}

/**
 * @brief Retrieves the list of CPUs shared with the OS (non-isolated cores).
 * This method calculates the complement of the isolated cores. Any CPU core
 * that is active but not explicitly marked as isolated will be considered shared.
 * Optimization attributes [[unlikely]] are used for error handling paths.
 * @return const std::vector<int>& A reference to the vector containing shared CPU IDs.
 */
const std::vector<int>& UndoSys::getSharedCpu()
{
   if (_sharedCoresChecked) {
      return _sharedCores;
   }

   int totCpu = getTotalCpu();
   if (totCpu == -1) [[unlikely]] {
      return _sharedCores;
   }

   // Pull a fresh local copy or reference to ensure isolation is parsed
   const std::vector<int>& isolated = getIsolatedCpu();
   size_t numOfIsolated = isolated.size();

   if (!numOfIsolated) {
      _sharedCores.clear();
      for (int i = 0; i < totCpu; ++i) {
         _sharedCores.push_back(i);
      }
      _sharedCoresChecked = true;
      return _sharedCores;
   }

   _sharedCores.clear();

   // Fill missing gaps between sorted isolated cores
   for (size_t i = 0; i < numOfIsolated; ++i) {
      if (i == 0) {
         // Gap from core 0 to the first isolated core
         for (int j = 0; j < isolated[i]; ++j) {
            _sharedCores.push_back(j);
         }
      } else {
         // Gap between consecutive isolated cores
         for (int j = isolated[i - 1] + 1; j < isolated[i]; ++j) {
            _sharedCores.push_back(j);
         }
      }
   }

   // Gap from the last isolated core up to the maximum system CPU count
   for (int j = isolated[numOfIsolated - 1] + 1; j < totCpu; ++j) {
      _sharedCores.push_back(j);
   }

   _sharedCoresChecked = true;
   return _sharedCores;
}

/**
 * @brief Busy wait useful to test accurate time passed
 * 
 * @param ns: Wait time in ns
 */
void UndoSys::busyWait(uint64_t ns)
{
   uint64_t start = readTsc();
   while (tsc2Ns(readTsc() - start) < ns) {
      // Prevent the while optimization
      asm volatile("" : : : "memory");
   }
}

// =============== Private methods ===============
/**
 * @brief Helper method to write a string value to a specific sysfs attributes path.
 * @param path The full path to the sysfs file.
 * @param value The string value to write.
 * @return true if successful, false otherwise.
 */
bool UndoSys::writeSysfsAttribute(const std::string& path, const std::string& value)
{
   std::ofstream file(path);
   if (!file.is_open()) {
      UndoLog& logger = UndoLog::getInstance();
      logger.logRT(LogDomain::PLC, LOG_ERR, "UndoSys: Failed to open sysfs path: %s  (Root privileges required)", path.data());
      return false;
   }
   file << value;
   return file.good();
}