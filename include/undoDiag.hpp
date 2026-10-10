/**
 * @file undoDiag.hpp
 * @brief Real-time safe diagnostics hub: a lock-free bridge between a non-RT
 *        observer (an OPC UA servent, a test, a recorder) and the RT cycle.
 * @author Salvatore Bamundo
 * @date October 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 *
 * @details
 * The design follows the same rule as undoLog: the RT thread never touches a
 * socket, a lock or the heap. It only pops fixed-size POD commands and pushes
 * fixed-size POD samples through two single-producer/single-consumer lock-free
 * queues. Everything that can block, allocate or fail lives on the observer
 * side.
 *
 * The variable layout is *data*, not generated accessor functions. st2cpp walks
 * its symbol table once, emits a flat array of Node descriptors, and this
 * header walks that table generically. Offsets are computed by the compiler via
 * offsetof(), so a layout change cannot silently desynchronise the table from
 * the structs.
 *
 * Direction of the two queues is asymmetric on purpose:
 *
 *   observer thread --[ CommandQueue ]--> Hub::process() (RT, end of cycle)
 *   observer thread <--[ SampleQueue  ]-- Hub::process() (RT, end of cycle)
 *
 * Because boost::lockfree::spsc_queue assumes exactly one producer and one
 * consumer per queue, the observer side must be a single thread.
 */

#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <vector>
#include <boost/lockfree/spsc_queue.hpp>

namespace undoDiag {

// Hard limits. They size the lock-free records, so they are compile-time
// constants and not tuning knobs: a record with a variable-length payload would
// need the heap, which the RT thread must not touch.
constexpr uint32_t MAX_DIMS = 4;            // array dimensions per node
constexpr uint32_t MAX_PAYLOAD = 64;        // bytes per command/sample chunk
constexpr uint32_t MAX_FORCES = 32;         // concurrent forced nodes
constexpr uint32_t MAX_SUBSCRIPTIONS = 64;  // concurrent subscriptions
constexpr uint32_t MAX_DEPTH = 32;          // parent chain length (nested structs)
constexpr uint32_t NO_PARENT = 0xFFFFFFFFu; // Node.parent sentinel: this is a root
constexpr uint32_t NO_NODE = 0xFFFFFFFFu;   // lookup miss sentinel

/**
 * @enum Op
 * @brief What the observer asks the RT cycle to do.
 */
enum class Op : uint8_t {
   Read,        // resolve and publish the current value
   Write,       // one-shot store, overwritten by the program next cycle
   Force,       // sticky store, re-applied every cycle until Release
   Release,     // drop a sticky force
   Subscribe,   // publish the value at the end of every cycle, including this one
   Unsubscribe, // stop publishing a subscription
};

/**
 * @enum Quality
 * @brief Why a sample carries the value it carries.
 */
enum class Quality : uint8_t {
   Good,   // value read straight from the variable
   Bad,    // request rejected or variable unavailable
   Forced, // value currently overridden by a force
};

/**
 * @enum SampleFlags
 * @brief Per-sample bit flags.
 */
enum class SampleFlags : uint8_t {
   None = 0,
   LastChunk = 1, // last sample of a multi-chunk value
   Ack = 2,       // zero-length result for a mutating command
};

/**
 * @enum NodeFlags
 * @brief Per-node capability bits, emitted by the compiler.
 */
enum class NodeFlags : uint8_t {
   None = 0,
   Readable = 1,
   Writable = 2,
   Forceable = 4,
};

/**
 * @enum NodeKind
 * @brief What the node contributes during address resolution.
 */
enum class NodeKind : uint8_t {
   Scalar, // leaf scalar value, size is its sizeof
   Array,  // indexed node, size is whole array, elemSize is one element
   Struct, // aggregate, size is sizeof, children are its members
   Root,   // a bound program instance, children are its globals
};

inline constexpr NodeFlags operator|(NodeFlags a, NodeFlags b)
{
   return static_cast<NodeFlags>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

inline constexpr bool hasFlag(NodeFlags value, NodeFlags flag)
{
   return (static_cast<uint8_t>(value) & static_cast<uint8_t>(flag)) != 0;
}

inline constexpr SampleFlags operator|(SampleFlags a, SampleFlags b)
{
   return static_cast<SampleFlags>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

/**
 * @struct Node
 * @brief One entry of the flat reflection table emitted by the compiler.
 *
 * @details
 * A node's offset is relative to its parent's *element base*: for a struct
 * parent that is the struct itself, for an array parent it is the selected
 * element. Resolving `arr[i].field` therefore walks root -> arr -> field,
 * applying arr's indices before adding field's offset.
 *
 * Arrays are one node with up to MAX_DIMS dimensions, matching the nested
 * STArray layout (row-major, contiguous). elemSize is the innermost scalar
 * size, size is the whole array size = elemSize * product(dims).
 *
 * Only trivially copyable leaves may be marked Readable/Writable. std::string
 * and other owning types must be excluded by the emitter: memcpy on them is the
 * only operation this hub knows.
 */
struct Node
{
   const char* name;       // short member name, e.g. "SPEED"
   const char* path;       // canonical unique path, stable across builds
   uint32_t parent;        // NO_PARENT for a root
   uint32_t firstChild;    // index of the first child, if any
   uint16_t childCount;    // number of children
   uint8_t kind;           // NodeKind
   uint8_t flags;          // NodeFlags
   uint8_t dimCount;       // dimensions, 0 for non-arrays
   uint8_t reserved;       // padding, keeps the struct layout explicit
   uint32_t offset;        // byte offset from the parent's element base
   uint32_t size;          // bytes of this node's whole value
   uint32_t elemSize;      // bytes per array element, 0 if not an array
   int32_t low[MAX_DIMS];  // per-dimension lower bound
   int32_t high[MAX_DIMS]; // per-dimension upper bound
};

/**
 * @struct Command
 * @brief Observer -> RT request. Fixed size, no heap.
 */
struct Command
{
   uint64_t seq;          // observer correlation id, echoed in the result
   uint32_t node;         // target node index
   uint32_t offset;       // byte offset for partial Read/Write
   uint16_t len;          // valid bytes in payload (Write)
   uint8_t op;            // Op
   uint8_t nIdx;          // valid entries in idx
   int32_t idx[MAX_DIMS]; // array indices, outermost first
   uint8_t payload[MAX_PAYLOAD];
};

/**
 * @struct Sample
 * @brief RT -> observer value or acknowledgement. Fixed size, no heap.
 */
struct Sample
{
   uint64_t timestampNs; // RT timestamp, supplied by the caller
   uint64_t cycle;       // master cycle counter
   uint64_t seq;         // echoed Command.seq, 0 for subscriptions
   uint32_t node;        // source node index
   uint32_t offset;      // byte offset of this chunk inside the value
   uint16_t len;         // valid bytes in payload
   uint8_t quality;      // Quality
   uint8_t flags;        // SampleFlags
   uint8_t payload[MAX_PAYLOAD];
};

// spsc_queue requires trivially copyable elements to stay lock-free.
static_assert(std::is_trivially_copyable<Command>::value, "Command must be trivially copyable");
static_assert(std::is_trivially_copyable<Sample>::value, "Sample must be trivially copyable");

using CommandQueue = boost::lockfree::spsc_queue<Command, boost::lockfree::capacity<256>>;
using SampleQueue = boost::lockfree::spsc_queue<Sample, boost::lockfree::capacity<2048>>;

namespace detail {

struct ForceEntry
{
   uint32_t node;
   uint32_t len;
   uint8_t nIdx;
   int32_t idx[MAX_DIMS];
   uint8_t data[MAX_PAYLOAD];
};

struct Subscription
{
   uint32_t node;
   uint8_t nIdx;
   int32_t idx[MAX_DIMS];
};

} // namespace detail

/**
 * @class Hub
 * @brief Owns the reflection table, the force table and the two queues.
 *
 * Lifetime contract:
 *  - bindRoot() is called for every program instance before the RT thread starts,
 *    so the RT side reads immutable base pointers with no synchronisation.
 *  - process() is called at the end of every master cycle, on the RT thread.
 *  - the observer thread only pushes Commands and pops Samples, never calling
 *    into the Hub concurrently with itself.
 */
class Hub
{
public:
   Hub(const Node* nodes, uint32_t nodeCount, CommandQueue& commands, SampleQueue& samples)
      : _nodes(nodes), _nodeCount(nodeCount), _commands(commands), _samples(samples), _base(nodeCount, nullptr)
   {}

   /**
    * @brief Bind a root node to its live program object. Call before start().
    */
   bool bindRoot(uint32_t rootId, void* base)
   {
      if (rootId >= _nodeCount || base == nullptr) {
         return false;
      }
      if (_nodes[rootId].parent != NO_PARENT) {
         return false;
      }
      _base[rootId] = static_cast<uint8_t*>(base);
      return true;
   }

   /**
    * @brief Look a node up by its canonical path. Startup/tooling only, linear.
    */
   uint32_t findByPath(const char* path) const
   {
      if (path == nullptr) {
         return NO_NODE;
      }
      for (uint32_t i = 0; i < _nodeCount; ++i) {
         if (_nodes[i].path != nullptr && std::strcmp(_nodes[i].path, path) == 0) {
            return i;
         }
      }
      return NO_NODE;
   }

   uint32_t nodeCount() const { return _nodeCount; }
   const Node& node(uint32_t id) const { return _nodes[id]; }

   /**
    * @brief Re-apply all sticky forces to their targets.
    * @details Call this twice per cycle, on the RT thread, at the two points a
    * force must act on the physical process: once right after the inputs have
    * been read (undoMasterTask::onInputsRead) so the logic sees the forced
    * inputs, and once right before the outputs are written
    * (undoMasterTask::onBeforeOutputsWrite) so the fieldbus takes the forced
    * outputs. Re-applying every force at both points is correct without
    * classifying I/O: a force the logic or the input image overwrote during the
    * cycle is restored before either the bus copy or the observers read it. Must
    * not block, allocate or take a lock.
    *
    * Safe state wins over forces: the generated Master must stop calling
    * applyForces() (and call clearForces()) once safeStopHandler() has run, so a
    * forced TRUE can never overwrite the safe value the safe-stop handler wrote
    * to the bus. The default timeout path already breaks out of the cycle loop
    * right after safeStopHandler(), so the guard is what keeps the contract when
    * a derived Master keeps running instead.
    */
   void applyForces() { reapplyForces(); }

   /**
    * @brief Drop every sticky force without touching the variables.
    * @details Called on the safe-stop path so a stopped (or later restarted)
    * PLC cannot resurrect a stale forced value. No lock, no allocation.
    */
   void clearForces() { _forceCount = 0; }

   /**
    * @brief Pull every queue slot and this hub's fixed tables into memory.
    * @details The RT Master is pinned to an isolated CPU and its cycle must not
    * take a page fault, so the pages behind CommandQueue, SampleQueue and the
    * force/subscription tables have to be resident before the threads start.
    * Wrap each queue once (push to full, then empty it - one rotation touches
    * every cell) and touch the fixed arrays, then the caller mlockall()s the
    * process so the pages stay resident. Startup only, never on the RT path.
    */
   void prefault()
   {
      Command cmd{};
      while (_commands.push(cmd)) {
      }
      Sample sample{};
      while (_samples.push(sample)) {
      }
      Command popCmd;
      while (_commands.pop(popCmd)) {
      }
      Sample popSample;
      while (_samples.pop(popSample)) {
      }

      volatile uint8_t sink = 0;
      for (uint32_t i = 0; i < MAX_FORCES; ++i) {
         sink ^= _forces[i].data[0];
      }
      for (uint32_t i = 0; i < MAX_SUBSCRIPTIONS; ++i) {
         sink ^= static_cast<uint8_t>(_subscriptions[i].node);
      }
      for (uint32_t i = 0; i < _base.size(); ++i) {
         sink ^= _base[i] != nullptr ? 1 : 0;
      }
      (void) sink;
   }

   /**
    * @brief Serve pending commands, re-apply forces and publish subscriptions.
    *
    * Called by the RT master at the end of every cycle. Never blocks and never
    * allocates. timestampNs and cycle are supplied by the caller so this header
    * stays independent of undoSystem and undoLog.
    */
   void process(uint64_t cycle, uint64_t timestampNs)
   {
      // Restore sticky forces first, so every read and every subscription in
      // this cycle observes the forced value even if the program overwrote it.
      // With a Master that also wires onInputsRead()/onBeforeOutputsWrite() this
      // is a redundant third restore, kept so a Hub used without those hooks
      // still reports the forced view to its observers.
      reapplyForces();

      Command command;
      while (_commands.pop(command)) {
         dispatch(command, cycle, timestampNs);
      }

      publishSubscriptions(cycle, timestampNs);
   }

private:
   void dispatch(const Command& command, uint64_t cycle, uint64_t timestampNs)
   {
      // One gate for the whole command set: nIdx is used as a bound on the
      // idx array of every op, so a malformed client cannot walk past the
      // MAX_DIMS entries the record actually carries.
      if (command.nIdx > MAX_DIMS) {
         emitAck(command.node, command.seq, cycle, timestampNs, Quality::Bad);
         return;
      }
      switch (static_cast<Op>(command.op)) {
      case Op::Read:
         handleRead(command, cycle, timestampNs);
         break;
      case Op::Write:
         handleWrite(command, cycle, timestampNs);
         break;
      case Op::Force:
         handleForce(command, cycle, timestampNs);
         break;
      case Op::Release:
         handleRelease(command, cycle, timestampNs);
         break;
      case Op::Subscribe:
         handleSubscribe(command, cycle, timestampNs);
         break;
      case Op::Unsubscribe:
         handleUnsubscribe(command, cycle, timestampNs);
         break;
      }
   }

   // ------------------------------------------------------------------
   // Address resolution
   // ------------------------------------------------------------------

   /**
    * @brief Multiply out one array node's indices into a byte offset.
    * @return false if any index is out of range.
    */
   bool indexOffset(const Node& n, const int32_t* idx, uint32_t& outOffset) const
   {
      int64_t offset = 0;
      for (uint32_t d = 0; d < n.dimCount; ++d) {
         int32_t i = idx[d];
         if (i < n.low[d] || i > n.high[d]) {
            return false;
         }
         uint32_t stride = n.elemSize;
         for (uint32_t m = d + 1; m < n.dimCount; ++m) {
            stride *= static_cast<uint32_t>(n.high[m] - n.low[m] + 1);
         }
         offset += static_cast<int64_t>(i - n.low[d]) * stride;
      }
      outOffset = static_cast<uint32_t>(offset);
      return true;
   }

   /**
    * @brief Resolve a node plus optional indices to a live byte address.
    * @param outBytes Bytes available at the returned address (whole value, or
    *                 element size when the target array was fully indexed).
    * @return nullptr on any invalid node, unbound root, missing/misplaced index.
    */
   uint8_t* resolve(uint32_t id, const int32_t* idx, uint32_t nIdx, uint32_t* outBytes) const
   {
      if (id >= _nodeCount) {
         return nullptr;
      }

      // Collect the root..target chain, target first, then reverse the walk.
      uint32_t path[MAX_DEPTH];
      uint32_t depth = 0;
      uint32_t current = id;
      while (true) {
         if (depth >= MAX_DEPTH) {
            return nullptr;
         }
         path[depth++] = current;
         uint32_t parent = _nodes[current].parent;
         if (parent == NO_PARENT) {
            break;
         }
         if (parent >= _nodeCount) {
            return nullptr;
         }
         current = parent;
      }

      uint8_t* base = _base[path[depth - 1]];
      if (base == nullptr) {
         return nullptr;
      }

      uint8_t* address = base;
      uint32_t dim = 0;
      for (int32_t k = static_cast<int32_t>(depth) - 1; k >= 0; --k) {
         const Node& v = _nodes[path[k]];

         if (k < static_cast<int32_t>(depth) - 1) {
            const Node& parent = _nodes[path[k + 1]];
            if (parent.kind == static_cast<uint8_t>(NodeKind::Array)) {
               if (dim + parent.dimCount > nIdx) {
                  return nullptr;
               }
               uint32_t element = 0;
               if (!indexOffset(parent, idx + dim, element)) {
                  return nullptr;
               }
               address += element;
               dim += parent.dimCount;
            }
            address += v.offset;
         }

         // The target itself may be an array read element-wise.
         if (k == 0 && v.kind == static_cast<uint8_t>(NodeKind::Array) && dim < nIdx) {
            if (dim + v.dimCount != nIdx) {
               return nullptr;
            }
            uint32_t element = 0;
            if (!indexOffset(v, idx + dim, element)) {
               return nullptr;
            }
            address += element;
            dim += v.dimCount;
            if (outBytes != nullptr) {
               *outBytes = v.elemSize;
            }
            return address;
         }
      }

      if (dim != nIdx) {
         return nullptr;
      }
      if (outBytes != nullptr) {
         *outBytes = _nodes[id].size;
      }
      return address;
   }

   // ------------------------------------------------------------------
   // Sample emission
   // ------------------------------------------------------------------

   void pushSample(const Sample& sample)
   {
      // A full queue drops the sample instead of blocking the RT thread. The
      // observer is expected to keep up; losing telemetry never loses control.
      _samples.push(sample);
   }

   void emitAck(uint32_t nodeId, uint64_t seq, uint64_t cycle, uint64_t timestampNs, Quality quality)
   {
      Sample sample{};
      sample.timestampNs = timestampNs;
      sample.cycle = cycle;
      sample.seq = seq;
      sample.node = nodeId;
      sample.quality = static_cast<uint8_t>(quality);
      sample.flags = static_cast<uint8_t>(SampleFlags::Ack);
      pushSample(sample);
   }

   void emitChunks(uint32_t nodeId, const uint8_t* data, uint32_t len, uint64_t seq, uint64_t cycle, uint64_t timestampNs, Quality quality)
   {
      uint32_t offset = 0;
      do {
         uint32_t chunk = std::min(MAX_PAYLOAD, len - offset);
         Sample sample{};
         sample.timestampNs = timestampNs;
         sample.cycle = cycle;
         sample.seq = seq;
         sample.node = nodeId;
         sample.offset = offset;
         sample.len = static_cast<uint16_t>(chunk);
         sample.quality = static_cast<uint8_t>(quality);
         sample.flags = (offset + chunk >= len) ? static_cast<uint8_t>(SampleFlags::LastChunk) : 0;
         std::memcpy(sample.payload, data + offset, chunk);
         if (!_samples.push(sample)) {
            return;
         }
         offset += chunk;
      } while (offset < len);
   }

   // ------------------------------------------------------------------
   // Command handlers
   // ------------------------------------------------------------------

   void handleRead(const Command& command, uint64_t cycle, uint64_t timestampNs)
   {
      uint32_t bytes = 0;
      uint8_t* address = resolve(command.node, command.idx, command.nIdx, &bytes);
      if (address == nullptr) {
         emitAck(command.node, command.seq, cycle, timestampNs, Quality::Bad);
         return;
      }
      if (command.offset > bytes) {
         emitAck(command.node, command.seq, cycle, timestampNs, Quality::Bad);
         return;
      }

      uint32_t available = bytes - command.offset;
      uint32_t len = (command.len == 0 || command.len > available) ? available : command.len;
      Quality quality = isForced(command.node) ? Quality::Forced : Quality::Good;
      emitChunks(command.node, address + command.offset, len, command.seq, cycle, timestampNs, quality);
   }

   void handleWrite(const Command& command, uint64_t cycle, uint64_t timestampNs)
   {
      uint32_t bytes = 0;
      uint8_t* address = resolve(command.node, command.idx, command.nIdx, &bytes);
      bool allowed = address != nullptr && hasFlag(static_cast<NodeFlags>(_nodes[command.node].flags), NodeFlags::Writable);
      if (!allowed || command.len > MAX_PAYLOAD || command.offset > bytes || command.len > bytes - command.offset) {
         emitAck(command.node, command.seq, cycle, timestampNs, Quality::Bad);
         return;
      }
      std::memcpy(address + command.offset, command.payload, command.len);
      emitAck(command.node, command.seq, cycle, timestampNs, Quality::Good);
   }

   void handleForce(const Command& command, uint64_t cycle, uint64_t timestampNs)
   {
      uint32_t bytes = 0;
      uint8_t* address = resolve(command.node, command.idx, command.nIdx, &bytes);
      bool allowed = address != nullptr && hasFlag(static_cast<NodeFlags>(_nodes[command.node].flags), NodeFlags::Forceable) && bytes > 0
                     && bytes <= MAX_PAYLOAD;
      if (!allowed) {
         emitAck(command.node, command.seq, cycle, timestampNs, Quality::Bad);
         return;
      }
      // The whole resolved element is overridden, so the request must carry that
      // many bytes: a partial force could never be released coherently, and
      // trusting command.len over memcpy would copy random tail bytes into the
      // variable. An empty len means "the whole element", the same convention a
      // GetObject read uses.
      if (command.len != 0 && command.len != bytes) {
         emitAck(command.node, command.seq, cycle, timestampNs, Quality::Bad);
         return;
      }
      if (findForceMatch(command.node, command.nIdx, command.idx) < 0 && _forceCount >= MAX_FORCES) {
         emitAck(command.node, command.seq, cycle, timestampNs, Quality::Bad);
         return;
      }

      std::memcpy(address, command.payload, bytes);
      upsertForce(command, bytes);
      emitAck(command.node, command.seq, cycle, timestampNs, Quality::Forced);
   }

   void handleRelease(const Command& command, uint64_t cycle, uint64_t timestampNs)
   {
      // Release is by node: it drops every forced element of that node, so the
      // observer does not have to remember the exact index path it used.
      removeForces(command.node);
      emitAck(command.node, command.seq, cycle, timestampNs, Quality::Good);
   }

   void handleSubscribe(const Command& command, uint64_t cycle, uint64_t timestampNs)
   {
      // A subscription that cannot resolve on day one is a subscription that will
      // publish a Bad ack every cycle: refuse it now instead of creating garbage.
      uint32_t bytes = 0;
      if (resolve(command.node, command.idx, command.nIdx, &bytes) == nullptr) {
         emitAck(command.node, command.seq, cycle, timestampNs, Quality::Bad);
         return;
      }
      if (findSubscription(command.node) < 0) {
         if (_subscriptionCount >= MAX_SUBSCRIPTIONS) {
            emitAck(command.node, command.seq, cycle, timestampNs, Quality::Bad);
            return;
         }
         detail::Subscription& sub = _subscriptions[_subscriptionCount++];
         sub.node = command.node;
         sub.nIdx = command.nIdx;
         for (uint32_t i = 0; i < MAX_DIMS; ++i) {
            sub.idx[i] = command.idx[i];
         }
      }
      emitAck(command.node, command.seq, cycle, timestampNs, Quality::Good);
   }

   void handleUnsubscribe(const Command& command, uint64_t cycle, uint64_t timestampNs)
   {
      int32_t index = findSubscription(command.node);
      if (index >= 0) {
         _subscriptions[index] = _subscriptions[--_subscriptionCount];
      }
      emitAck(command.node, command.seq, cycle, timestampNs, Quality::Good);
   }

   // ------------------------------------------------------------------
   // Force table
   // ------------------------------------------------------------------

   int32_t findForceMatch(uint32_t nodeId, uint8_t nIdx, const int32_t* idx) const
   {
      for (uint32_t i = 0; i < _forceCount; ++i) {
         const detail::ForceEntry& entry = _forces[i];
         if (entry.node != nodeId || entry.nIdx != nIdx) {
            continue;
         }
         bool same = true;
         for (uint8_t d = 0; d < nIdx; ++d) {
            if (entry.idx[d] != idx[d]) {
               same = false;
               break;
            }
         }
         if (same) {
            return static_cast<int32_t>(i);
         }
      }
      return -1;
   }

   bool isForced(uint32_t nodeId) const
   {
      for (uint32_t i = 0; i < _forceCount; ++i) {
         if (_forces[i].node == nodeId) {
            return true;
         }
      }
      return false;
   }

   void upsertForce(const Command& command, uint32_t len)
   {
      int32_t index = findForceMatch(command.node, command.nIdx, command.idx);
      if (index < 0) {
         index = static_cast<int32_t>(_forceCount++);
      }
      detail::ForceEntry& entry = _forces[index];
      entry.node = command.node;
      entry.len = len;
      entry.nIdx = command.nIdx;
      for (uint32_t i = 0; i < MAX_DIMS; ++i) {
         entry.idx[i] = command.idx[i];
      }
      std::memcpy(entry.data, command.payload, len);
   }

   void removeForces(uint32_t nodeId)
   {
      // swap-remove while iterating, order does not matter.
      uint32_t i = 0;
      while (i < _forceCount) {
         if (_forces[i].node == nodeId) {
            _forces[i] = _forces[--_forceCount];
         } else {
            ++i;
         }
      }
   }

   void reapplyForces()
   {
      for (uint32_t i = 0; i < _forceCount; ++i) {
         const detail::ForceEntry& entry = _forces[i];
         uint32_t bytes = 0;
         uint8_t* address = resolve(entry.node, entry.idx, entry.nIdx, &bytes);
         if (address != nullptr && bytes >= entry.len) {
            std::memcpy(address, entry.data, entry.len);
         }
      }
   }

   // ------------------------------------------------------------------
   // Subscriptions
   // ------------------------------------------------------------------

   int32_t findSubscription(uint32_t nodeId) const
   {
      for (uint32_t i = 0; i < _subscriptionCount; ++i) {
         if (_subscriptions[i].node == nodeId) {
            return static_cast<int32_t>(i);
         }
      }
      return -1;
   }

   void publishSubscriptions(uint64_t cycle, uint64_t timestampNs)
   {
      for (uint32_t i = 0; i < _subscriptionCount; ++i) {
         const detail::Subscription& sub = _subscriptions[i];
         uint32_t bytes = 0;
         uint8_t* address = resolve(sub.node, sub.idx, sub.nIdx, &bytes);
         if (address == nullptr) {
            emitAck(sub.node, 0, cycle, timestampNs, Quality::Bad);
            continue;
         }
         Quality quality = isForced(sub.node) ? Quality::Forced : Quality::Good;
         emitChunks(sub.node, address, bytes, 0, cycle, timestampNs, quality);
      }
   }

   const Node* _nodes;
   uint32_t _nodeCount;
   CommandQueue& _commands;
   SampleQueue& _samples;
   std::vector<uint8_t*> _base; // live program pointer per root node

   detail::ForceEntry _forces[MAX_FORCES];
   uint32_t _forceCount{0};

   detail::Subscription _subscriptions[MAX_SUBSCRIPTIONS];
   uint32_t _subscriptionCount{0};
};

} // namespace undoDiag
