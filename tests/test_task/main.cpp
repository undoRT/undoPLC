/**
 * @file main.cpp
 * @brief Test application for UndoMasterTaskBase / UndoWorkerTaskBase fork-join infrastructure.
 * @author Salvatore Bamundo
 * @date June 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#include "undoTasks.hpp"
#include "undoSystem.hpp"
#include "undoLog.hpp"
#include <algorithm>
#include <csignal>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>

// ============================================================================
//  DemoWorkerTask: simulates a compiled PRG block with a fixed busy-wait load.
//  In production this is where st2cpp-generated logic (or any user PRG) runs.
//  ATTENTION: So far (Look file date) does not exist a proper undoOS!
//  This test has been executed in a ubuntu preempt-rt with no any special bios config.
//  The pc in which you execute this test should have preempt-rt, at least 2 isolated cores and
//  a minimal grub configuration to optimize core isolation and frequency.
// ============================================================================
class DemoWorkerTask : public UndoWorkerTaskBase
{
public:
   DemoWorkerTask(UndoMasterTaskBase* master, uint16_t cpuId, uint16_t prio, uint32_t workNs)
      : UndoWorkerTaskBase(master, cpuId, prio), _workNs(workNs)
   {}

   // It should be the FIRST instruction of the distructor, before that vtable goes to
   // UndoWorkerTaskBase and before that _workNs/_cycleCount will be destroyed.
   ~DemoWorkerTask() override { shutdownAndJoin(); }

protected:
   bool runStartup() override
   {
      UndoLog::getInstance()
         .logRT(LogDomain::PRG, LOG_INFO, "%s: startup on core %u (simulated load: %u ns)", _taskName.c_str(), _cpuId, _workNs);
      return true;
   }

   bool runWork() override
   {
      // Placeholder for real PRG logic. Deterministic busy-wait emulates a fixed
      // computational workload without relying on syscalls or allocations.
      UndoSys::getInstance().busyWait(_workNs);
      ++_cycleCount;
      return true;
   }

   void runFinish() override
   {
      UndoLog::getInstance().logRT(LogDomain::PRG,
                                   LOG_INFO,
                                   "%s: finished after %llu cycles",
                                   _taskName.c_str(),
                                   static_cast<unsigned long long>(_cycleCount));
   }

private:
   uint32_t _workNs;
   uint64_t _cycleCount{0};
};

// ============================================================================
//  DemoMasterTask: drives the fieldbus cycle and periodically logs diagnostics.
//  readInputBus()/writeOutputBus() are placeholders for real fieldbus I/O
//  (e.g. undoBUS / EtherCAT PDO exchange).
// ============================================================================
class DemoMasterTask : public UndoMasterTaskBase
{
public:
   using UndoMasterTaskBase::UndoMasterTaskBase;

   // CRITICO: deve essere la PRIMA istruzione del distruttore, prima che la
   // vtable regredisca a UndoMasterTaskBase (che ha readInputBus/writeOutputBus/
   // safeStopHandler come pure virtual). Senza questa chiamata esplicita,
   // il thread RT può ancora invocare una di queste funzioni nella finestra
   // di race aperta da ~UndoMasterTaskBase() -> "pure virtual method called".
   ~DemoMasterTask() override { shutdownAndJoin(); }

protected:
   bool runStartup() override
   {
      UndoLog::getInstance().logRT(LogDomain::PLC, LOG_INFO, "%s: startup on core %u", _taskName.c_str(), _cpuId);
      return true;
   }

   void readInputBus() override
   {
      // Placeholder: real fieldbus read (e.g. EtherCAT PDO) goes here.
   }

   void writeOutputBus() override
   {
      // Placeholder: real fieldbus write goes here.
      // Print a diagnostic snapshot once per second (assuming a 1ms cycle).
      if (++_cycleCount % 1000 == 0) {
         const DiagVars& d = getDiagVars();
         UndoLog::getInstance().logRT(LogDomain::PLC,
                                      LOG_INFO,
                                      "%s: cycle %llu (%lu mS)| exec[min=%u max=%u]us jitter[min=%u max=%u]us",
                                      _taskName.c_str(),
                                      static_cast<unsigned long long>(_cycleCount),
                                      getCurrentCycleTimeMs(),
                                      d.execMin,
                                      d.execMax,
                                      d.jitterMin,
                                      d.jitterMax);
      }
   }

   void safeStopHandler() override
   {
      UndoLog::getInstance().logRT(LogDomain::PLC, LOG_ERR, "%s: SAFE STOP triggered - forcing outputs to safe state", _taskName.c_str());
      // Placeholder: real fieldbus safe-state write goes here.
   }

   void runFinish() override
   {
      UndoLog::getInstance().logRT(LogDomain::PLC,
                                   LOG_INFO,
                                   "%s: finished after %llu cycles",
                                   _taskName.c_str(),
                                   static_cast<unsigned long long>(_cycleCount));
   }

private:
   uint64_t _cycleCount{0};
};

int main(int argc, char* argv[])
{
   bool logToConsole = false;
   for (int i = 1; i < argc; ++i) {
      if (std::string(argv[i]) == "--log2console") {
         logToConsole = true;
      }
   }

   // 1. Create the central asynchronous execution context (main thread, non-RT)
   boost::asio::io_context ioc;

   UndoSys& sys = UndoSys::getInstance();
   UndoLog& logger = UndoLog::getInstance();

   // Register the main thread BEFORE init(), like every other test in this repo.
   logger.registerThread();
   logger.init(ioc, logToConsole);

   std::cout << "====================================================" << std::endl;
   std::cout << "   undoPLC - UndoMasterTaskBase / UndoWorkerTaskBase Test   " << std::endl;
   std::cout << "====================================================" << std::endl;

   const std::vector<int>& isolated = sys.getIsolatedCpu();
   logger.logRT(LogDomain::PLC, LOG_INFO, "Isolated CPUs detected: %zu", isolated.size());

   // We need at least 2 isolated cores: 1 for the Master + >=1 for Worker(s).
   if (isolated.size() < 2) {
      std::cerr << "[ERROR] Need at least 2 isolated CPUs (got " << isolated.size() << "). Check your GRUB isolcpus= configuration."
                << std::endl;
      return 1;
   }

   bool setSuccess = sys.setCpuNominalFrequency(isolated);
   if (!setSuccess) {
      std::cerr << "[ERROR] For some reason is not possible to set CPU nominal frequency to isolated CPUs!" << std::endl;
      ioc.run();
      return 1;
   }

   constexpr uint64_t cycleTimeNs = 1'000'000;   // 1ms cycle
   constexpr uint32_t simulatedWorkNs = 100'000; // 100us of simulated PRG logic per worker
   constexpr uint16_t masterPrio = 90;
   constexpr uint16_t workerPrio = 80; // strictly lower than the master

   // Reserve one isolated core for the Master; spawn up to 2 demo workers
   // on the remaining isolated cores (adjust to your actual core count).
   const size_t maxWorkers = isolated.size() - 1;
   const size_t workerCount = std::min<size_t>(maxWorkers, 2);

   // Construct the Master FIRST (does not start the thread yet).
   DemoMasterTask master(cycleTimeNs);

   // Construct workers on explicit isolated cores. Each constructor call
   // auto-registers itself into master's worker pool (registerWorker()),
   // and this MUST happen before master.start() is called.
   std::vector<std::unique_ptr<DemoWorkerTask>> workers;
   for (size_t i = 0; i < workerCount; ++i) {
      int cpu = isolated[isolated.size() - 1 - i]; // take from the tail of the list
      workers.push_back(std::make_unique<DemoWorkerTask>(&master, static_cast<uint16_t>(cpu), workerPrio, simulatedWorkNs));
   }

   // Start the Master: it auto-resolves an isolated CPU not already taken
   // by any registered worker (see UndoMasterTaskBase::start()).
   if (!master.start(masterPrio)) {
      std::cerr << "[ERROR] Failed to start UndoMasterTaskBase. Check logs for details." << std::endl;
      return 1;
   }

   for (auto& w : workers) {
      if (!w->start()) {
         std::cerr << "[ERROR] Failed to start " << w->getName() << std::endl;
         return 1;
      }
   }

   logger.logRT(LogDomain::PLC, LOG_INFO, "Master on core %u | %zu worker(s) spawned", master.getCpuId(), workers.size());

   // 2. Graceful shutdown on Ctrl+C / SIGTERM, handled reactively on the main thread.
   boost::asio::signal_set signals(ioc, SIGINT, SIGTERM);
   signals.async_wait([&](const boost::system::error_code& ec, int signalNumber) {
      if (!ec) {
         logger.logRT(LogDomain::PLC, LOG_WARNING, "Termination signal (%d) received. Stopping RT threads...", signalNumber);
         // master.stop() also propagates _running=false to every registered worker
         // and notifies both condition variables to break any blocked wait.
         master.stop();
         bool resetSuccess = sys.resetCpuFrequency(isolated);
         if (resetSuccess) {
            std::cout << "-> SUCCESS: System power states restored correctly." << std::endl;
         } else {
            std::cerr << "-> ERROR: Failed to reset some cores back to powersave!" << std::endl;
         }
         ioc.stop();
      }
   });

   std::cout << "[Main] RT threads running. Press Ctrl+C to stop." << std::endl;

   // 3. Main thread blocks here servicing the logger reactively (0% busy-poll).
   ioc.run();

   // Destruction order: 'workers' (declared after 'master') is destroyed FIRST,
   // joining every worker thread cleanly; only then 'master' is destroyed.
   std::cout << "[Main] Test finished cleanly." << std::endl;
   return 0;
}