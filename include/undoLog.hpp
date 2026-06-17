/**
 * @file undoLog.hpp
 * @brief Real-time deferred logging subsystem using lock-free architecture.
 * @author Salvatore Bamundo
 * @date June 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#pragma once

#include <cstdint>
#include <cstdarg>
#include <cstdio>
#include <syslog.h>
#include <boost/lockfree/spsc_queue.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/posix/stream_descriptor.hpp>
#include <boost/asio/signal_set.hpp>
#include <unordered_map>
#include <mutex>
#include <memory>
#include <thread>

// Static configuration for Rel-Time
constexpr size_t LOG_MSG_MAX_LEN = 128;     // Maximum buffer message dimension
constexpr size_t LOG_QUEUE_CAPACITY = 1024; // Maximum number of messages into the queue

/**
 * @enum LogDomain
 * @brief Identify the message origin for the file routing.
 */
enum class LogDomain : uint8_t {
   BUS, // For undoBUS.log (Fieldbus)
   PLC, // Per undoPLC.log (Base infrastructure)
   PRG  // Per undoPRG.log (User program logic)
};

/**
 * @struct LogRecord
 * @brief Fixed dimensione struct allocated in stack/queue without usage of heap. 
 */
struct LogRecord
{
   uint64_t timestampNs;
   int logLevel;
   LogDomain domain;
   char message[LOG_MSG_MAX_LEN];
};

class UndoLog
{
public:
   // Remove Copy/move constructors for singleton
   UndoLog(const UndoLog&) = delete;
   UndoLog& operator=(const UndoLog&) = delete;
   UndoLog(UndoLog&&) = delete;
   UndoLog& operator=(UndoLog&&) = delete;
   static UndoLog& getInstance()
   {
      static UndoLog instance;
      return instance;
   }

   void init(boost::asio::io_context& ioc, bool log2console);
   void logRT(LogDomain domain, int level, const char* format, ...) __attribute__((format(printf, 4, 5)));
   void registerThread();
   void closeRegistration();

   using SpscQueue = boost::lockfree::spsc_queue<LogRecord, boost::lockfree::capacity<LOG_QUEUE_CAPACITY>>;

private:
   UndoLog() {}
   ~UndoLog();

   void processLogs();
   void startAsyncRead();

   bool _log2console{false};
   int _eventFd{-1};

   // Core pre-allocated lock-free ring buffer: one for each thread (Thread-Local Storage pattern with Lock-Free drain)
   std::unordered_map<std::thread::id, std::unique_ptr<SpscQueue>> _queues;
   // Used only in a startup field, not in RT
   std::mutex _registrationMutex;
   std::atomic<bool> _registrationOpen{true};

   // Non-copyable/movable OS descriptors managed by unique_ptr to allow safe Singleton lifecycle
   std::unique_ptr<boost::asio::posix::stream_descriptor> _streamDesc{nullptr};

   // Static 8-byte buffer required for eventfd atomic counter accumulation
   uint64_t _eventfdBuffer{0};
};