#include <iostream>

#include "framework/inspector.h"
#include "framework/module.h"
#include "protocols/packet.h"
#include "pub_sub/appid_event_ids.h"
#include "pub_sub/http_event_ids.h"
#include "pub_sub/intrinsic_event_ids.h"
#include "sfip/sf_ip.h"

#include "common.rs.h"
#include "network_mapping.rs.h"
#include "rust.h"

using namespace snort;

static const Parameter nm_params[] = {
    {"cache_size", Parameter::PT_INT, "0:max32", "0", "set cache size"},
    {"log_file", Parameter::PT_STRING, nullptr, "log.txt",
     "set output file name"},
    {nullptr, Parameter::PT_MAX, nullptr, nullptr, nullptr}};

class NetworkMappingModule : public Module {
public:
  NetworkMappingModule()
      : Module("network_mapping",
               "Help map resources in the network based on their comms",
               nm_params) {}

  Usage get_usage() const override { return GLOBAL; }

  bool set(const char *c, snort::Value &val, snort::SnortConfig *) override {
    if (val.is("log_file") && val.get_string()) {
      set_log_file(val.get_string());
    }

    return true;
  }
};

class NetworkMappingInspector : public Inspector {
  NetworkMappingModule *module;

  void eval(Packet *packet) override {
    assert(packet);
    eval_packet(*packet);
  }

public:
  NetworkMappingInspector(NetworkMappingModule *module) : module(module) {}

  class EventHandler : public snort::DataHandler {
    const char *c;

  public:
    EventHandler(const char *c) : DataHandler("network_mapping"), c(c) {}

    void handle(snort::DataEvent &event, snort::Flow *flow) override {
      std::cout << "++ EventHandler::handle called(" << c << ")" << std::endl;
      if (flow) {
        handle_event(event, *flow);
      }
    }
  };

  bool configure(SnortConfig *sc) override {
    DataBus::subscribe_network(
        intrinsic_pub_key, IntrinsicEventIds::FLOW_STATE_SETUP,
        new EventHandler("IntrinsicEventIds::FLOW_STATE_SETUP"));
    DataBus::subscribe_network(
        intrinsic_pub_key, IntrinsicEventIds::FLOW_STATE_RELOADED,
        new EventHandler("IntrinsicEventIds::FLOW_STATE_RELOADED"));
    DataBus::subscribe_network(
        intrinsic_pub_key, IntrinsicEventIds::AUXILIARY_IP,
        new EventHandler("IntrinsicEventIds::AUXILIARY_IP"));
    DataBus::subscribe_network(
        intrinsic_pub_key, IntrinsicEventIds::PKT_WITHOUT_FLOW,
        new EventHandler("IntrinsicEventIds::PKT_WITHOUT_FLOW"));

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
    IT_PROBE,
    PROTO_BIT__ALL, // PROTO_BIT__ANY_IP, // PROTO_BIT__ALL, PROTO_BIT__NONE, //
    nullptr,        // buffers
    nullptr,        // service
    nullptr,        // pinit
    nullptr,        // pterm
    nullptr,        // tinit
    nullptr,        // tterm
    [](Module *module) -> Inspector * {
      return new NetworkMappingInspector((NetworkMappingModule *)module);
    },
    [](Inspector *p) { delete p; },
    nullptr, // ssn
    nullptr  // reset
};

#include "common.rs.cc"
#include "network_mapping.rs.cc"
#include "x_event.cc"
#include "x_flow.cc"

SO_PUBLIC const BaseApi *snort_plugins[] = {&networkmap_api.base, nullptr};
