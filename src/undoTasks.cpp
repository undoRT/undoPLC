/**
 * @file undoTasks.cpp
 * @brief Implementation of deterministic Master/Worker synchronized execution loops.
 * @author Salvatore Bamundo
 * @date June 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#include "undoTasks.hpp"

// =============== UndoMasterTaskBase Implementation ===============

/**
 * @brief Construct a new Undo Master Task object.
 * @param cycleTimeNs Execution cycle period in nanoseconds.
 */
UndoMasterTaskBase::UndoMasterTaskBase(uint64_t cycleTimeNs)
{
   _cycleTimeNs = cycleTimeNs;
   _taskName = "undoMasterTask";
}

/**
 * @brief Destroy the Undo Master Task:: Undo Master Task object
 */
UndoMasterTaskBase::~UndoMasterTaskBase()
{
   /*
    * If this assert is triggered, a derived class has forgot to call
    * shutdownAndJoin() in its own destructor... (use it properly!!!!!)
    */
   assert(!_thread.joinable()
          && "Derived class of UndoMasterTaskBase must call shutdownAndJoin() "
             "as the FIRST statement in its own destructor!");
   shutdownAndJoin();
}

/**
 * @brief Registers a worker task into the internal master synchronization pool.
 * It can be called ONLY before the start() method.
 * @param worker Pointer to the worker task instance.
 */
void UndoMasterTaskBase::registerWorker(UndoWorkerTaskBase* worker)
{
   assert(!_running && "registerWorker() called even if UndoMasterTaskBase is running!");
   _syncVars.workers.push_back(worker);
}

/**
 * @brief Resolves an available isolated CPU, sets affinity, configures SCHED_FIFO, and launches the thread.
 * @param prio Real-time priority layer (1-99).
 */
bool UndoMasterTaskBase::start(uint16_t prio)
{
   UndoSys& sys = UndoSys::getInstance();
   UndoLog& logger = UndoLog::getInstance();

   if (_running.load(std::memory_order_relaxed)) {
      return false;
   }

   _prio = prio;
   const std::vector<int>& isolatedCpus = sys.getIsolatedCpu();

   // Collect all CPU IDs currently requested by registered workers
   std::vector<int> takenCpus;
   for (auto* worker : _syncVars.workers) {
      takenCpus.push_back(worker->getCpuId());
   }

   // Find the first isolated CPU that is NOT taken by any worker
   bool cpuFound = false;
   for (int isoCpu : isolatedCpus) {
      if (std::find(takenCpus.begin(), takenCpus.end(), isoCpu) == takenCpus.end()) {
         _cpuId = static_cast<uint16_t>(isoCpu);
         cpuFound = true;
         break;
      }
   }

   if (!cpuFound) {
      logger.logRT(LogDomain::PLC, LOG_ERR, "UndoMasterTaskBase: Critical - No available isolated CPU found for Master Task.");
      // Fallback strategy or failure behavior can be anchored here
      return false;
   }

   // Initialize the starting value of latch counter
   int totalThreads = 1 + static_cast<int>(_syncVars.workers.size());
   _registrationLatch = std::make_unique<std::latch>(totalThreads);

   _running.store(true, std::memory_order_release);

   // Spawn the native underlying thread context using a lambda execution binding
   _thread = std::thread([this]() {
      // Enforce strict CPU affinity mask inside the executing context
      cpu_set_t cpuset;
      CPU_ZERO(&cpuset);
      CPU_SET(_cpuId, &cpuset);
      pthread_t currentThread = pthread_self();

      if (pthread_setaffinity_np(currentThread, sizeof(cpu_set_t), &cpuset) != 0) {
         UndoLog::getInstance().logRT(LogDomain::PLC, LOG_ERR, "UndoMasterTaskBase: Failed to set CPU affinity on core %u", _cpuId);
      }

      // Elevate scheduling policy to SCHED_FIFO for real-time determinism
      struct sched_param param;
      param.sched_priority = _prio;
      if (pthread_setschedparam(currentThread, SCHED_FIFO, &param) != 0) {
         UndoLog::getInstance().logRT(LogDomain::PLC, LOG_ERR, "UndoMasterTaskBase: Failed to set SCHED_FIFO priority %u", _prio);
      }

      // Register this task and decrement the latch
      UndoLog::getInstance().registerThread();
      _registrationLatch->count_down();

      // Jump into the cyclic execution engine
      this->run();
   });

   // Do not detach this thread here, it could be a problem... do it in destructor
   return true;
}

/**
 * @brief Signals execution termination and forces global wakeups to unblock resources.
 */
void UndoMasterTaskBase::stop()
{
   if (!_running.load(std::memory_order_relaxed)) {
      return;
   }

   _running.store(false, std::memory_order_release);

   // Secure lock before setting flags and signaling to prevent destruction race conditions
   {
      std::lock_guard<UndoMutex> lock(_syncVars.syncMutex);
      for (auto* worker : _syncVars.workers) {
         if (worker) {
            worker->setRunning(false);
         }
      }
   }
   _syncVars.workerCv.notify_all();
   _syncVars.masterCv.notify_all();
}

/**
 * @brief High-precision cyclic execution loop bound to Linux timers.
 */
void UndoMasterTaskBase::run()
{
   UndoSys& sys = UndoSys::getInstance();
   UndoLog& logger = UndoLog::getInstance();

   int totalWorkers = static_cast<int>(_syncVars.workers.size());
   uint64_t cycleStartTsc = 0;
   bool firstCycle = true;

   if (!runStartup()) {
      logger.logRT(LogDomain::PLC, LOG_ERR, "%s: runStartup() finished with error!", _taskName.c_str());
      _running.store(false, std::memory_order_release);
   }

   struct timespec baseTime;
   // Get the current time using the monotonic clock to avoid jump adjustments
   clock_gettime(CLOCK_MONOTONIC, &baseTime);
   // Compute base timestamp in nanoseconds
   uint64_t baseNs = baseTime.tv_sec * 1000000000ULL + baseTime.tv_nsec;
   // Align to the next multiple of the cycle
   uint64_t alignNs = _cycleTimeNs - (baseNs % _cycleTimeNs);
   if (alignNs == _cycleTimeNs) {
      alignNs = 0;
   }
   baseNs += alignNs;

   // Start af a wait of _STARTUP_DELAY_CYCLES to ensure that the system
   // has the time to stabilize and that all the workers are ready
   baseNs += (_cycleTimeNs * _STARTUP_DELAY_CYCLES);

   // Set the first cycle time
   _currentCycleTimeNs.store(baseNs, std::memory_order_release);

   // Prepare the next wakeup
   struct timespec nextWakeup;
   nextWakeup.tv_sec = baseNs / 1000000000ULL;
   nextWakeup.tv_nsec = baseNs % 1000000000ULL;

   while (_running.load(std::memory_order_relaxed)) {
      // Calculate next absolute deadline time point
      uint64_t currentCycleNs = _currentCycleTimeNs.load(std::memory_order_acquire);
      uint64_t nextCycleNs = currentCycleNs + _cycleTimeNs;

      nextWakeup.tv_sec = nextCycleNs / 1000000000ULL;
      nextWakeup.tv_nsec = nextCycleNs % 1000000000ULL;

      if (!firstCycle) {
         uint64_t execUs = sys.tsc2Ns(sys.readTsc() - cycleStartTsc) / 1000ULL;
         if (execUs > _diagVars.execMax) {
            _diagVars.execMax = execUs;
         }
         if (execUs < _diagVars.execMin) {
            _diagVars.execMin = execUs;
         }
      }
      // Ultra-precise hardware sleeping till next period block
      int ret = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &nextWakeup, nullptr);
      if (ret != 0) [[unlikely]] {
         logger.logRT(LogDomain::PLC, LOG_WARNING, "UndoMasterTaskBase: clock_nanosleep interrupted or errored: %d", ret);
         continue;
      }
      // Timestamp for execution time diag
      cycleStartTsc = sys.readTsc();

      // It is the current cycle
      uint64_t currentAbsoluteNs = _currentCycleTimeNs.load(std::memory_order_acquire) + _cycleTimeNs;
      _currentCycleTimeNs.store(currentAbsoluteNs, std::memory_order_release);

      // Jitter measurment respect to the actual time
      struct timespec actualWake;
      clock_gettime(CLOCK_MONOTONIC, &actualWake);
      int64_t jitterNs = (actualWake.tv_sec - nextWakeup.tv_sec) * 1000000000LL + (actualWake.tv_nsec - nextWakeup.tv_nsec);
      uint64_t jitterUs = jitterNs > 0 ? static_cast<uint64_t>(jitterNs) / 1000ULL : 0;

      if (jitterUs > _diagVars.jitterMax) {
         _diagVars.jitterMax = jitterUs;
      }
      if (jitterUs < _diagVars.jitterMin) {
         _diagVars.jitterMin = jitterUs;
      }

      // Process fieldbus read operations
      readInputBus();

      // Initialize synchronization state and trigger workers (Fork phase)
      if (totalWorkers > 0) {
         {
            std::lock_guard<UndoMutex> lock(_syncVars.syncMutex);
            _syncVars.activeWorkers.store(totalWorkers, std::memory_order_release);
            _syncVars.cycleCounter.fetch_add(1, std::memory_order_relaxed); // Signal a new cycle step
         }
         // Wake up all workers simultaneously
         _syncVars.workerCv.notify_all();

         // High-precision conditional wait with cycle watchdog (Join phase)
         std::unique_lock<UndoMutex> lock(_syncVars.syncMutex);
         auto timeoutDuration = std::chrono::nanoseconds(_cycleTimeNs);

         bool success = _syncVars.masterCv.wait_for(lock, timeoutDuration, [this]() {
            return _syncVars.activeWorkers.load(std::memory_order_acquire) == 0;
         });

         if (!success) [[unlikely]] {
            // Watchdog Triggered: one or more workers went into a while(1) loop or overflowed the execution window
            lock.unlock();
            onCycleTimeout();
            break;
         }
      }

      // Process fieldbus write operations (Outputs are written even if a worker timed out, or in safe state)
      writeOutputBus();
      firstCycle = false;
   }
   runFinish();
   logger.logRT(LogDomain::PLC, LOG_WARNING, "%s: Finished task", _taskName.c_str());
}

/**
 * @brief Default fallback implementation for cycle overrun watchdogs.
 */
void UndoMasterTaskBase::onCycleTimeout()
{
   UndoLog& logger = UndoLog::getInstance();
   logger.logRT(LogDomain::PLC, LOG_ERR, "UndoMasterTaskBase: Watchdog Trip! Real-time execution cycle overrun detected.");
   // Emergency actions like setting safe fieldbus outputs should be executed here
   safeStopHandler();
   stop();
}

/**
 * @brief DEVE essere chiamato dalla classe più derivata, come PRIMA istruzione
 * del proprio distruttore, prima che qualunque membro derivato venga distrutto.
 *
 * Il thread RT chiama funzioni virtuali su `this` per tutta la sua vita. Se è
 * ancora in esecuzione quando ~UndoMasterTaskBase() viene eseguito, il tipo dinamico
 * dell'oggetto è GIA' regredito a UndoMasterTaskBase (regola di distruzione C++),
 * quindi una chiamata virtuale concorrente atterra su uno stub pure-virtual.
 */

/**
 * @brief This MUST be called by the most derived class as the FIRST statement in its own
 * destructor, before any derived members are destroyed.
 *
 * The RT thread calls virtual functions on 'this' for the lifetime of the
 * object. If it is still running when ~UndoMasterTaskBase() executes, the
 * object's dynamic type has already reverted to UndoMasterTaskBase (C++
 * destruction rule), so a concurrent virtual call lands on a pure-virtual
 * stub.
 */
void UndoMasterTaskBase::shutdownAndJoin()
{
   stop();
   if (_thread.joinable() && _thread.get_id() != std::this_thread::get_id()) {
      _thread.join();
   }
}

// =============== UndoWorkerTaskBase Implementation ===============

/**
 * @brief Core loop for worker tasks waiting for Master activation triggers.
 */
UndoWorkerTaskBase::UndoWorkerTaskBase(UndoMasterTaskBase* master, uint16_t cpuId, uint16_t prio)
{
   UndoSys& sys = UndoSys::getInstance();
   UndoLog& logger = UndoLog::getInstance();

   _cpuId = cpuId;
   _taskName = "undoWorkerTask_" + std::to_string(_cpuId);
   _master = master;

   if (!_master) {
      logger.logRT(LogDomain::PLC, LOG_ERR, "%s: Created without a proper UndoMasterTaskBase", _taskName.c_str());
      return;
   }

   _masterIsPresent = true;
   _master->registerWorker(this);

   const std::vector<int>& isolatedCpu = sys.getIsolatedCpu();
   bool isCpuIsolated = false;
   for (const auto& cpu : isolatedCpu) {
      if (cpu == _cpuId) {
         logger.logRT(LogDomain::PLC, LOG_INFO, "%s: cpu %u is isolated", _taskName.c_str(), cpuId);
         isCpuIsolated = true;
      }
   }
   if (!isCpuIsolated) {
      logger.logRT(LogDomain::PLC, LOG_WARNING, "%s: cpu %u is shared", _taskName.c_str(), cpuId);
   }

   _prio = prio;
}

/**
 * @brief Destroy the Undo Master Task:: Undo Master Task object
 */
UndoWorkerTaskBase::~UndoWorkerTaskBase()
{
   /*
    * If this assert is triggered, a derived class has forgot to call
    * shutdownAndJoin() in its own destructor... (use it properly!!!!!)
    */
   assert(!_thread.joinable()
          && "Derived class of UndoMasterTaskBase must call shutdownAndJoin() "
             "as the FIRST statement in its own destructor!");
   shutdownAndJoin();
}

/**
 * @brief Spawns the underlying execution thread, assigning hardware affinity masks and real-time schedules.
 */
bool UndoWorkerTaskBase::start()
{
   if (!_masterIsPresent) {
      return false;
   }

   if (_running.load(std::memory_order_relaxed)) {
      return false;
   }

   _running.store(true, std::memory_order_release);

   _thread = std::thread([this]() {
      cpu_set_t cpuset;
      CPU_ZERO(&cpuset);
      CPU_SET(_cpuId, &cpuset);
      pthread_t currentThread = pthread_self();

      if (pthread_setaffinity_np(currentThread, sizeof(cpu_set_t), &cpuset) != 0) {
         UndoLog::getInstance().logRT(LogDomain::PLC, LOG_ERR, "%s: Failed to set CPU affinity on core %u", _taskName.c_str(), _cpuId);
      }

      struct sched_param param;
      param.sched_priority = _prio;
      if (pthread_setschedparam(currentThread, SCHED_FIFO, &param) != 0) {
         UndoLog::getInstance().logRT(LogDomain::PLC, LOG_ERR, "%s: Failed to set SCHED_FIFO priority %u", _taskName.c_str(), _prio);
      }

      // Register this task and decrement the latch
      UndoLog::getInstance().registerThread();
      _master->countDownRegistration();

      // Invoke the internal execution framework
      this->run();
   });

   // Like master task, do not detach here...
   return true;
}

/**
 * @brief Signals termination requests down to the isolated worker loop context.
 */
void UndoWorkerTaskBase::stop()
{
   _running.store(false, std::memory_order_release);
   _master->getSyncVars().workerCv.notify_all();
}

/**
 * @brief Cyclic body of the UndoWorkerTaskBase
 */
void UndoWorkerTaskBase::run()
{
   UndoSys& sys = UndoSys::getInstance();
   UndoLog& logger = UndoLog::getInstance();

   if (!_master) {
      UndoLog::getInstance().logRT(LogDomain::PLC, LOG_ERR, "%s: Cannot run without master!", _taskName.c_str());
      return;
   }

   uint64_t currentMasterCycle = 0;
   uint64_t t1 = 0;
   uint64_t t2 = 0;

   if (!runStartup()) {
      logger.logRT(LogDomain::PLC, LOG_ERR, "%s: runStartup() finished with error!", _taskName.c_str());
      _running.store(false, std::memory_order_relaxed);
   }

   while (_running.load(std::memory_order_relaxed)) {
      // Enter suspended state until Master signals start
      std::unique_lock<UndoMutex> lock(_master->getSyncVars().syncMutex);
      _master->getSyncVars().workerCv.wait(lock, [this, &currentMasterCycle]() {
         currentMasterCycle = _master->getSyncVars().cycleCounter.load(std::memory_order_relaxed);
         return (currentMasterCycle > _lastProcessedCycle) || (!_running.load(std::memory_order_relaxed));
      });

      if (!_running.load(std::memory_order_relaxed)) [[unlikely]] {
         break;
      }

      _cycleTimeNs = _master->getCycleNs();
      _currentCycleTimeNs = _master->getCurrentCycleTimeNs();
      _lastProcessedCycle = currentMasterCycle;

      lock.unlock(); // Release lock before running heavy logic to allow maximum concurrent scaling

      t1 = sys.readTsc();

      // Execute compiled user/system logic block (e.g. st2cpp outputs)
      bool ok = runWork();

      t2 = sys.tsc2Ns(sys.readTsc() - t1) / 1000ULL;
      if (t2 > _diagVars.jitterMax) {
         _diagVars.jitterMax = t2;
      } else if (t2 < _diagVars.jitterMin) {
         _diagVars.jitterMin = t2;
      }

      // Coordinate termination tracking (Join acknowledgment phase)
      lock.lock();
      // Atomic decrement with acquire-release memory ordering
      if (_master->getSyncVars().activeWorkers.fetch_sub(1, std::memory_order_acq_rel) == 1) {
         // This was the last worker executing logic, wake up the master thread
         _master->getSyncVars().masterCv.notify_one();
      }

      if (!ok) {
         logger.logRT(LogDomain::PLC, LOG_ERR, "%s: runWork() finished with error!", _taskName.c_str());
         lock.unlock(); // Release lock before break
         break;
      }
   }
   runFinish();
   logger.logRT(LogDomain::PLC, LOG_WARNING, "%s: Finished task", _taskName.c_str());
}

/**
 * @brief This MUST be called by the most derived class as the FIRST statement in its own
 * destructor, before any derived members are destroyed.
 *
 * The RT thread calls virtual functions on 'this' for the lifetime of the
 * object. If it is still running when ~UndoMasterTaskBase() executes, the
 * object's dynamic type has already reverted to UndoMasterTaskBase (C++
 * destruction rule), so a concurrent virtual call lands on a pure-virtual
 * stub.
 */
void UndoWorkerTaskBase::shutdownAndJoin()
{
   stop();
   if (_thread.joinable() && _thread.get_id() != std::this_thread::get_id()) {
      _thread.join();
   }
}