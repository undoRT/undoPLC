/**
 * @file main.cpp
 * @brief Main execution loop driven by Boost.Asio event execution engine.
 * @author Salvatore Bamundo
 * @date June 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#include "undoSystem.hpp"
#include "undoLog.hpp"
#include <iostream>
#include <string>

int main(int argc, char* argv[])
{
   bool logToConsole = false;
   for (int i = 1; i < argc; ++i) {
      if (std::string(argv[i]) == "--log2console") {
         logToConsole = true;
      }
   }

   // 1. Create the central asynchronous execution context
   boost::asio::io_context ioc;

   UndoSys& sys = UndoSys::getInstance();
   UndoLog& logger = UndoLog::getInstance();

   // Register this thread
   logger.registerThread();

   // Pass the io_context reference to the logger subsystem
   logger.init(ioc, logToConsole);

   std::cout << "====================================================" << std::endl;
   std::cout << "          Initialize Engine undoRT (Asio Active)    " << std::endl;
   std::cout << "====================================================" << std::endl;

   logger.logRT(LogDomain::PLC, LOG_INFO, "Engine undoRT initialized. Total CPU cores detected: %d", sys.getTotalCpu());

   // 2. Register OS Signals asynchronously using Boost.Asio
   boost::asio::signal_set signals(ioc, SIGINT, SIGTERM);
   signals.async_wait([&ioc, &logger](const boost::system::error_code& error, int signal_number) {
      if (!error) {
         logger.logRT(LogDomain::PLC, LOG_WARNING, "Termination signal (%d) intercepted. Stopping engine...", signal_number);
         // Stop the io_context loop. This causes ioc.run() to return instantly.
         ioc.stop();
      }
   });

   // ========================================================================
   // PLACEHOLDER: Launch background Real-Time threads here
   // They will communicate with this loop via logger.logRT()
   // ========================================================================
   logger.logRT(LogDomain::PLC, LOG_INFO, "Entering reactive Boost.Asio execution loop.");

   // 3. THE EVENT LOOP (Exact counterpart to Qt's app.exec())
   // Blocks the main thread with 0% CPU overhead, handling triggers reactively.
   ioc.run();

   std::cout << "[Main] Asio event loop stopped. Execution finished clean." << std::endl;
   return 0;
}