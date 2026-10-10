/**
 * @file undoTasks.hpp
 * @brief Core real-time task framework for Master and Worker synchronization.
 * @author Salvatore Bamundo
 * @date June 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#pragma once

#include "undoLog.hpp"
#include "undoSystem.hpp"
#include "undoMutex.hpp"
#include "undoLatch.hpp"
#include <condition_variable>
#include <undoCore/ioBus.hpp>
#include <undoCore/processImage.hpp>

class UndoWorkerTaskBase;

// Synchronization variables
struct SyncVars
{
   std::vector<UndoWorkerTaskBase*> workers;          // Vector containing all the worker' pointers
   alignas(64) std::atomic<int> activeWorkers{0};     // Counter of active workers
   std::condition_variable_any masterCv;              // Condition variable for the Master thread
   std::condition_variable_any workerCv;              // Condition variable for Worker threads
   alignas(64) std::atomic<uint64_t> cycleCounter{0}; // Shared cycle token to avoid spurious wakeups
   UndoMutex syncMutex;
};

/**
 * @brief Basic struct to make some diag
 */
struct DiagVars
{
   uint32_t jitterMax{0};
   uint32_t jitterMin{0xFFFFFFFF};
   uint32_t execMax{0};
   uint32_t execMin{0xFFFFFFFF};
};

// clang-format off
/**
 * @class UndoMasterTaskBase
 * @brief Base class responsible for fieldbus orchestration and worker synchronization.
 */
class UndoMasterTaskBase
{
public:
   UndoMasterTaskBase(uint64_t cycleTimeNs);
   virtual ~UndoMasterTaskBase();

   bool start(uint16_t prio = 99);
   void stop();
   void registerWorker(UndoWorkerTaskBase* worker);
   inline SyncVars& getSyncVars() { return _syncVars; }
   inline const DiagVars& getDiagVars() { return _diagVars; }
   inline void resetDiagVars() { _diagVars = DiagVars(); }
   inline uint16_t getCpuId() { return _cpuId; }
   inline uint16_t getPrio() { return _prio; }
   inline const std::string& getName() { return _taskName; }
   inline uint16_t getCycleUs() { return _cycleTimeNs / 1000; }
   inline uint16_t getCycleNs() { return _cycleTimeNs; }
   inline uint64_t getCurrentCycleTimeNs() const { return _currentCycleTimeNs.load(std::memory_order_acquire); }
   inline uint64_t getCurrentCycleTimeUs() const { return _currentCycleTimeNs.load(std::memory_order_acquire) / 1000ULL; }
   inline uint64_t getCurrentCycleTimeMs() const { return _currentCycleTimeNs.load(std::memory_order_acquire) / 1000000ULL; }
   void waitAllRegistered() { _registrationLatch->wait(); }
   void countDownRegistration() { _registrationLatch->count_down(); }
   void setIoBus(undoCore::IoBus* ioBus) { _ioBus = ioBus; }

protected:
   int waitCycle(timespec& nextWakeup);
   virtual void readInputBus();
   virtual void writeOutputBus();
   /**
    * @brief Hook called on the RT thread right after readInputBus(), before the
    *        workers run.
    * @details
    * Runs every cycle once the input image is in, so a forced input (or a forced
    * variable the logic reads) is visible to the workers this cycle. A force
    * cannot be applied earlier, inside process(), and still drive the logic: by
    * then the workers have already run. Default is a no-op so existing Masters
    * are unaffected. Override it to call undoDiag::Hub::applyForces(); it must
    * not block, allocate or take a lock.
    */
   virtual void onInputsRead() {}
   /**
    * @brief Hook called on the RT thread right before writeOutputBus(), after
    *        the workers have joined.
    * @details
    * Runs every cycle once the workers are done, so a forced output is written
    * back into memory before the bus copy takes it out: this is what makes a
    * Force reach the physical process instead of only the observer. Default is a
    * no-op so existing Masters are unaffected. Override it to call
    * undoDiag::Hub::applyForces(); it must not block, allocate or take a lock.
    */
   virtual void onBeforeOutputsWrite() {}
   /**
    * @brief Set the fieldbus outputs to their safe values.
    * @details Runs from onCycleTimeout() (watchdog trip), after which run()
    * breaks out of the cycle loop: the output hooks are not called again. A
    * derived Master that keeps running after a safe stop must stop calling
    * applyForces() and clear the hub's forces itself, so a forced TRUE can never
    * overwrite the safe value this handler wrote to the bus. Safe state always
    * wins over forces.
    */
   virtual void safeStopHandler() = 0;
   /**
    * @brief End-of-cycle hook, called on the RT thread right after writeOutputBus().
    * @details
    * Runs every cycle once the workers have joined and the outputs are settled,
    * which is the only point where a diagnostics hub sees a coherent snapshot.
    * Default is a no-op so existing Masters are unaffected. Override it to call
    * undoDiag::Hub::process(); it must not block, allocate or take a lock.
    */
   virtual void onCycleEnd() {}
   virtual void onCycleTimeout();
   virtual bool runStartup() { return true; }
   virtual void runFinish() { return; }
   void shutdownAndJoin();
   std::string _taskName{""};
   uint16_t _cpuId, _prio;
private:
   void run();

   uint64_t _cycleTimeNs;
   undoCore::IoBus *_ioBus{nullptr};
   std::atomic<bool> _running{false};
   SyncVars _syncVars;
   DiagVars _diagVars;
   std::thread _thread;
   std::atomic<uint64_t> _currentCycleTimeNs{0};   // Absolute time aligned with the cycle
   static constexpr int _STARTUP_DELAY_CYCLES = 5; // Number of cycle to wait before starting
   std::unique_ptr<UndoLatch> _registrationLatch; // One-shot gate: opens once every task thread has registered
};

/**
 * @class UndoWorkerTaskBase
 * @brief Base class for concurrent execution units (PRGs).
 */
class UndoWorkerTaskBase
{
   friend class UndoMasterTaskBase;

public:
   UndoWorkerTaskBase(UndoMasterTaskBase* master, uint16_t cpuId, uint16_t prio);
   virtual ~UndoWorkerTaskBase();

   bool start();
   void stop();
   inline const DiagVars& getDiagVars() { return _diagVars; }
   inline void resetDiagVars() { _diagVars = DiagVars(); }
   inline uint16_t getCpuId() { return _cpuId; }
   inline uint16_t getPrio() { return _prio; }
   inline const std::string& getName() { return _taskName; }
   inline void setRunning(bool running) { _running.store(running, std::memory_order_release); }
   inline uint16_t getCycleUs() { return _cycleTimeNs / 1000; }
   inline uint16_t getCycleNs() { return _cycleTimeNs; }
   inline uint64_t getCurrentCycleTimeNs() const { return _currentCycleTimeNs; }
   inline uint64_t getCurrentCycleTimeUs() const { return _currentCycleTimeNs / 1000ULL; }
   inline uint64_t getCurrentCycleTimeMs() const { return _currentCycleTimeNs / 1000000ULL; }

protected:
   virtual bool runStartup() { return true; }
   virtual bool runWork() { return true; }
   virtual void runFinish() { return; }
   void shutdownAndJoin();
   std::string _taskName{""};
   uint16_t _cpuId, _prio;
   bool _masterIsPresent{false};

private:
   void run();

   UndoMasterTaskBase* _master{nullptr};
   std::atomic<bool> _running{false};
   uint64_t _lastProcessedCycle{0}; // Tracks the last processed cycle token
   uint64_t _currentCycleTimeNs{0}; // Absolute time aligned with the cycle
   uint64_t _cycleTimeNs;
   DiagVars _diagVars;
   std::thread _thread;
};
// clang-format on