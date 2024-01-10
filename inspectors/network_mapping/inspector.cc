#include <chrono>
#include <fstream>
#include <iostream>
#include <list>
#include <mutex>
#include <sstream>
#include <string>

#include "framework/inspector.h"
#include "framework/module.h"
#include "protocols/packet.h"
#include "pub_sub/appid_event_ids.h"
#include "pub_sub/http_event_ids.h"
#include "pub_sub/intrinsic_event_ids.h"
#include "sfip/sf_ip.h"

using namespace snort;

bool use_rotate_feature = true;

static const Parameter nm_params[] = {
  {"cache_size", Parameter::PT_INT, "0:max32", "0", "set cache size"},
  {"log_file", Parameter::PT_STRING, nullptr, "flow.txt",
   "set output file name"},
  {"size_rotate", Parameter::PT_BOOL, nullptr, "false", "If true rotates log file after x lines"},

  {nullptr, Parameter::PT_MAX, nullptr, nullptr, nullptr}
};

struct LogFileStats
{
    PegCount line_count;
    PegCount file_count;
};

static THREAD_LOCAL LogFileStats s_file_stats;

const PegInfo s_pegs[] =
{
    { CountType::SUM, "lines", "lines written" },
    { CountType::SUM, "files", "files opened" },

    { CountType::END, nullptr, nullptr }
};

class LogFile {

  std::mutex      mutex;
  std::ofstream   stream;                       // Stream logs are written to
  std::string     base_file_name;               // The base filename, i.e. without the timestamp extension
  unsigned        log_files_opened = 0;         // Count of logfiles that has been opened
  unsigned        log_lines_total = 0;          // Total number of log lines written (sum of lines written to all files)
  unsigned        log_lines_written = 0;        // Number of log lines written in the current file
  const unsigned  max_lines_pr_file = 1'000'000;  // When this number of lines has been written a new file will be written

  // Flush parameters
  const unsigned  lines_beween_flushes = 100; // Number of lines between flushes
  unsigned        lines_since_last_flush = 0; // Number of lines since last flush

  enum class State {
    initial,    // Initial state
    open,       // File is open and ready for use
    full,       // The current file is full
    aborted     // We have stopped writing to an actual file
  } state = State::initial;

  // TODO(mkr) make a flush based on time too

public:
  void set_file_name(const char *new_name) {
    std::scoped_lock guard(mutex);

    assert(State::initial == state);    // We can't set the filename after we have started to use the name
    assert(new_name);                   // Make sure we got some input

    base_file_name = new_name;
  }

  void log(std::string message) noexcept {
    std::scoped_lock guard(mutex);

    switch (state) {
      case State::aborted:
        return;

      case State::full:
        stream.close();
        lines_since_last_flush = 0;

        [[fallthrough]];

      case State::initial: {
          using namespace std::chrono;
          assert(!base_file_name.empty());  // Logic error if the filename isn't set at this point

          std::string file_name(base_file_name);

          if (use_rotate_feature) {
            const auto cur_time = system_clock::now().time_since_epoch();
            uint64_t cur_time_ms = duration_cast<milliseconds>(cur_time).count();

            file_name += std::to_string(cur_time_ms);
          }

          // We use std::ios_base::app vs. std::ios_base::ate to ensure
          // we don't overwrite data written between our own writes
          stream.open(file_name, std::ios_base::app);

          if(!stream || !stream.is_open()) {
            state = State::aborted;
            return;
          }

          state = State::open;
          s_file_stats.file_count++;
          log_files_opened++;
          log_lines_written = 0;
        }

        [[fallthrough]];

      case State::open:
        //std::cout << "****** Logs: " << message << std::endl;
        stream << message << std::endl;

        s_file_stats.line_count++;
        log_lines_total++;
        log_lines_written++;
        lines_since_last_flush++;

        // TODO - validate that a stream with an error, can be closed, and reopened
        if (!stream || (use_rotate_feature && max_lines_pr_file <= log_lines_written)) {
          state = State::full;
        }
        else if (lines_beween_flushes <= lines_since_last_flush) {
          stream.flush();
          lines_since_last_flush = 0;
        }
    }
  }
};

// TODO(mkr) will a service client always equal the source, or can it equal the destination sometimes?
struct IpPacketCacheElement {
  std::chrono::time_point<std::chrono::steady_clock> create_time;

  SfIp      src_ip;
  uint16_t  src_port;
  SfIp      dst_ip;
  uint16_t  dst_port;
};


class IpPacketCache {
public:
  typedef void (*OrphanFunc)(SfIp src_ip, uint16_t src_port, SfIp dest_ip, uint16_t dst_port);
private:

  std::mutex  mutex;

  unsigned total_count = 0;
  unsigned total_orphan = 0;
  unsigned total_match = 0;
  unsigned total_failed_match = 0;

  const unsigned cur_max_size = 1;

  // We use a std::list for now, to get the rest of the logic in place
  // TODO(mkr) optimize - change to not allocate mem for each element
  std::list<IpPacketCacheElement> cache;

  OrphanFunc orphan;

public:
  IpPacketCache(OrphanFunc orphan) : orphan(orphan) {}

  ~IpPacketCache() {
    assert(0 != cache.size());  // Cache wasn't flushed before the end
  }

  void add(const Packet &p) {
    std::scoped_lock guard(mutex);

    // We add to the back so we can use the "for (autu itr:cache) {" statement later
    cache.emplace_back(IpPacketCacheElement{
      std::chrono::steady_clock::now(),
      *p.ptrs.ip_api.get_src(),
      p.ptrs.sp,
      *p.ptrs.ip_api.get_dst(),
      p.ptrs.dp}
    );

    total_count++;

    // If cache is full, remove oldest element
    if (cur_max_size < cache.size()) {
      auto const itr = cache.begin();
      // TODO(mkr): Make this call, without taking the mutex lock
      orphan(itr->src_ip, itr->src_port, itr->dst_ip, itr->dst_port);
      cache.pop_front();

      total_orphan++;
    };
  }

  void match(const SfIp &src_ip, const uint16_t src_port, const SfIp &dst_ip, const uint16_t dst_port) {
    std::scoped_lock guard(mutex);

    // TODO: Should we delete all, or just the first (oldest) match we find?
    for (auto itr = cache.begin(); itr != cache.end(); itr++) {
        if ( itr->src_ip == src_ip
          && itr->src_port == src_port
          && itr->dst_ip == dst_ip
          && itr->dst_port == dst_port ) {
        cache.erase(itr);

        total_match++;
        return;
      }
    }

    total_failed_match++;
  }

  void flush() {
    std::scoped_lock guard(mutex);

    for (auto itr : cache) {
      // TODO(mkr) make this call without holding the mutex
      orphan(itr.src_ip, itr.src_port, itr.dst_ip, itr.dst_port);
      total_orphan++;
    }

    cache.clear();
  }

  unsigned get_total_packet()       { return total_count; }
  unsigned get_total_orphan()       { return total_orphan; }
  unsigned get_total_match()        { return total_match; }
  unsigned get_total_failed_match() { return total_failed_match; }
};



class NetworkMappingModule : public Module {

public:
  NetworkMappingModule()
      : Module("network_mapping",
               "Help map resources in the network based on their comms",
               nm_params) {}

  LogFile logger;

  Usage get_usage() const override { return CONTEXT; }

  bool set(const char *, Value &val, SnortConfig *) override {
    if (val.is("log_file") && val.get_string()) {
      logger.set_file_name(val.get_string());
    }
    else if (val.is("size_rotate") )  {
      use_rotate_feature = val.get_bool();
    }


    return true;
  }

  const PegInfo* get_pegs() const override {
    return s_pegs;
  }

  PegCount* get_counts() const override {
    return (PegCount*)&s_file_stats;
  }
};

class NetworkMappingInspector : public Inspector {
public:
  NetworkMappingInspector(NetworkMappingModule *module) : module(*module) {}
  NetworkMappingModule &module;


  void eval(Packet *packet) override {
    if (packet) {
        if(packet->has_ip()) {
            char ip_str[INET_ADDRSTRLEN];
            std::stringstream ss;

            sfip_ntop(packet->ptrs.ip_api.get_src(), ip_str, sizeof(ip_str));
            ss << ip_str << ':' << packet->ptrs.sp << " -> ";

            sfip_ntop(packet->ptrs.ip_api.get_dst(), ip_str, sizeof(ip_str));
            ss << ip_str << ':' << packet->ptrs.dp;

            module.logger.log(ss.str());
        }
    }
  }

  class EventHandler : public DataHandler {
  public:
    EventHandler(NetworkMappingModule &module)
        : DataHandler("network_mapping"), module(module) {}
    NetworkMappingModule &module;

    void handle(DataEvent &, Flow *flow) override {
      assert(flow);


      char ip_str[INET_ADDRSTRLEN];
      std::stringstream ss;

      sfip_ntop(&flow->client_ip, ip_str, sizeof(ip_str));
      ss << ip_str << ':' << flow->client_port << " -> ";

      sfip_ntop(&flow->server_ip, ip_str, sizeof(ip_str));
      ss << ip_str << ':' << flow->server_port;

      ss << " - " << flow->service;

      module.logger.log(ss.str());

    }
  };

  bool configure(SnortConfig *) override {
    DataBus::subscribe_network(intrinsic_pub_key,
                               IntrinsicEventIds::FLOW_SERVICE_CHANGE,
                               new EventHandler(module));
    return true;
  }
};

const InspectApi networkmap_api = {
    {
        PT_INSPECTOR,
        sizeof(InspectApi),
        INSAPI_VERSION,
        0,
        API_RESERVED,
        API_OPTIONS,
        "network_mapping",
        "Help map resources in the network based on their comms",
        []() -> Module * { return new NetworkMappingModule; },
        [](Module *m) { delete m; },
    },

    IT_FIRST,
    PROTO_BIT__ALL, // PROTO_BIT__ANY_IP, // PROTO_BIT__ALL, PROTO_BIT__NONE, //
    nullptr,        // buffers
    nullptr,        // service
    nullptr,        // pinit
    nullptr,        // pterm
    nullptr,        // tinit
    nullptr,        // tterm
    [](Module *module) -> Inspector * {
      assert(module);
      return new NetworkMappingInspector(
          dynamic_cast<NetworkMappingModule *>(module));
    },
    [](Inspector *p) { delete p; },
    nullptr, // ssn
    nullptr  // reset
};

SO_PUBLIC const BaseApi *snort_plugins[] = {&networkmap_api.base, nullptr};
