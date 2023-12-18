#include <iostream>

#include "framework/inspector.h"
#include "framework/module.h"
#include "protocols/packet.h"
#include "pub_sub/appid_event_ids.h"
#include "pub_sub/http_event_ids.h"
#include "pub_sub/intrinsic_event_ids.h"
#include "sfip/sf_ip.h"

#include "test_packet/test_packet.rs.h"

using namespace snort;

// TODO(rdo) use this to parametrize the tests
static const Parameter nm_params[] = {
    {nullptr, Parameter::PT_MAX, nullptr, nullptr, nullptr}};

class TestModule : public Module {
public:
  TestModule()
      : Module("test_packet", "Tests the Rust wrapper around packets",
               nm_params) {}

  Usage get_usage() const override { return GLOBAL; }
};

class TestInspector : public Inspector {
  TestModule *module;

  void eval(Packet *packet) override {
    assert(packet);
    eval_packet(*packet);
  }

public:
  TestInspector(TestModule *module) : module(module) {}
};

const InspectApi test_packetapi = {
    {
        PT_INSPECTOR,
        sizeof(InspectApi),
        INSAPI_VERSION,
        0,
        API_RESERVED,
        API_OPTIONS,
        "test_packet",
        "Tests the Rust wrapper around packets",
        []() -> Module * { return new TestModule; },
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
      return new TestInspector((TestModule *)module);
    },
    [](Inspector *p) { delete p; },
    nullptr, // ssn
    nullptr  // reset
};

SO_PUBLIC const BaseApi *snort_plugins[] = {&test_packetapi.base, nullptr};