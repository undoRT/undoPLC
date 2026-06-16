/**
 * @file undoLog.cpp
 * @brief Source implementation for real-time deferred logging.
 * @author Salvatore Bamundo
 * @date June 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#include "undoLog.hpp"
#include "undoSystem.hpp"

UndoLog::~UndoLog()
{
   closelog();
   if (_eventFd != -1) {
      close(_eventFd);
   }
}

/**
 * @brief Initializes the eventfd descriptor and binds it to the Boost.Asio context.
 * @param ioc Reference to the main execution context.
 * @param log2console Standard output mirroring flag.
 */
void UndoLog::init(boost::asio::io_context& ioc, bool log2console)
{
   _log2console = log2console;

   // Connection to Linux local syslog
   // LOG_PID includes the process ID in any log line
   // logFile will be chosen during syslog
   openlog(nullptr, LOG_PID | LOG_CONS, LOG_USER);

   // Create a Linux eventfd counter descriptor.
   // EFD_NONBLOCK ensures the RT thread never blocks on signals.
   _eventFd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
   if (_eventFd == -1) {
      std::perror("[UndoLog] Failed to create eventfd");
      std::exit(EXIT_FAILURE);
   }

   // Wrap the raw file descriptor inside the reactive Boost.Asio stream wrapper
   _streamDesc = std::make_unique<boost::asio::posix::stream_descriptor>(ioc, _eventFd);

   // Arm the first asynchronous read trigger
   startAsyncRead();
}

/**
 * @brief Write a deferred log (lock-free). It is safe for thread RT.
 * @param domain Log origin (LogDomain::BUS, PLC, PRG).
 * @param level POSIX priority level (eg. LOG_INFO, LOG_ERR).
 * @param format printf format string.
 */
void UndoLog::logRT(LogDomain domain, int level, const char* format, ...)
{
   LogRecord record;
   record.logLevel = level;
   record.domain = domain;

   UndoSys& sys = UndoSys::getInstance();
   record.timestampNs = sys.tsc2Ns(sys.readTsc());

   va_list args;
   va_start(args, format);
   vsnprintf(record.message, LOG_MSG_MAX_LEN, format, args);
   va_end(args);

   // Push into the safe pre-allocated queue
   _queue.push(record);

   // Notify the OS kernel eventfd counter.
   // Writing an 8-byte integer '1' increments the kernel counter and triggers epoll.
   // This is an atomic, non-blocking hardware-bound system call.
   if (_eventFd != -1) {
      uint64_t trigger = 1;
      long n = write(_eventFd, &trigger, sizeof(trigger));
      (void) n; // Suppress unused result warning safely inside RT loop
   }
}

/**
 * @brief Sets up the asynchronous reactive monitor for the eventfd file descriptor.
 */
void UndoLog::startAsyncRead()
{
   if (!_streamDesc) {
      return;
   }

   _streamDesc->async_read_some(boost::asio::buffer(&_eventfdBuffer, sizeof(_eventfdBuffer)),
                                [this](const boost::system::error_code& ec, std::size_t bytes_transferred) {
                                   if (!ec && bytes_transferred > 0) {
                                      // Wake up event caught: flush all records accumulated inside the queue
                                      this->processLogs();

                                      // Re-arm the asynchronous monitor loop (reactive chaining)
                                      this->startAsyncRead();
                                   }
                                });
}

/**
 * @brief Flushes all records within the lock-free queue into system destinations.
 */
void UndoLog::processLogs()
{
   LogRecord record;
   while (_queue.pop(record)) {
      const char* domainStr = "undoPLC";
      const char* colorCode = "\033[0m";

      switch (record.domain) {
      case LogDomain::BUS:
         domainStr = "undoBUS";
         colorCode = "\033[1;36m";
         break;
      case LogDomain::PLC:
         domainStr = "undoPLC";
         colorCode = "\033[1;32m";
         break;
      case LogDomain::PRG:
         domainStr = "undoPRG";
         colorCode = "\033[1;35m";
         break;
      }

      syslog(record.logLevel, "<%s> [%lu ns] %s", domainStr, record.timestampNs, record.message);

      if (_log2console) {
         if (record.logLevel <= LOG_ERR) {
            std::printf("\033[1;31m[ERR][%s][%lu ns] %s\033[0m\n", domainStr, record.timestampNs, record.message);
         } else if (record.logLevel == LOG_WARNING) {
            std::printf("\033[1;33m[WRN][%s][%lu ns] %s\033[0m\n", domainStr, record.timestampNs, record.message);
         } else {
            std::printf("%s[%s]\033[0m[%lu ns] %s\n", colorCode, domainStr, record.timestampNs, record.message);
         }
      }
   }
}