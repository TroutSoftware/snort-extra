

// Snort includes
#include <events/event.h>
#include <framework/module.h>
#include <log/messages.h>
#include <managers/inspector_manager.h>
#include <protocols/packet.h>

// System includes
#include <cassert>
#include <iostream>

// Local includes
#include "alert_lioli.h"
#include "lioli_tree_generator.h"
#include "log_lioli_tree.h"


namespace alert_lioli {
namespace {

static const char *s_name = "alert_lioli";
static const char *s_help =
    "lioli logger, will output through a log module compatible with lioli";

static const snort::Parameter module_params[] = {
    //{"~", snort::Parameter::PT_STRING, nullptr, nullptr, "unnamed option"},
    {"logger", snort::Parameter::PT_STRING, nullptr, nullptr,
     "Set logger output should be sent to"},
    {nullptr, snort::Parameter::PT_MAX, nullptr, nullptr, nullptr}};

class Module : public snort::Module {

  Module() : snort::Module(s_name, s_help, module_params) {}

  std::string logger_name;

  bool set(const char *s, snort::Value &val, snort::SnortConfig *) override {
    if (s)
      std::cout << "Exp: s is " << s << std::endl;
    if (val.is("logger") && val.get_as_string().size() > 0) {
      std::cout << "Exp: Using logger: " << val.get_as_string() << std::endl;
      logger_name = val.get_string();
      return true;
    }

    // fail if we didn't get something valid
    return false;
  }

  Usage get_usage() const override { return GLOBAL; }

public:
  std::string &get_logger_name() { return logger_name; }

  static snort::Module *ctor() { return new Module(); }
  static void dtor(snort::Module *p) { delete p; }
};

class Logger : public snort::Logger {
  LioLi::LogLioLiTree *logger = nullptr;
  Module &module;

  LioLi::LogLioLiTree &get_logger() {
    if (!logger) {
      auto mp = snort::InspectorManager::get_inspector(
          module.get_logger_name().c_str(), snort::Module::GLOBAL,
          snort::IT_PASSIVE);
      logger = dynamic_cast<LioLi::LogLioLiTree *>(mp);

      if (!logger) {
        snort::ErrorMessage(
            "ERROR: Alert lioli doesn't have a valid configured logger\n");

        return LioLi::LogLioLiTree::get_null_tree();
      }
    }

    return *logger;
  }

private:
  Logger(Module *module) : module(*module) {
    assert(module);
    std::cout << "Exp: Logger::Logger() called" << std::endl;
  }
  void open() override { std::cout << "Exp: open() called" << std::endl; }
  void close() override { std::cout << "Exp: close() called" << std::endl; }
  void reset() override { std::cout << "Exp: reset() called" << std::endl; }
  void reload() override { std::cout << "Exp: reload() called" << std::endl; }

  void alert(snort::Packet *pkt, const char *msg, const Event &) override {
    get_logger().log(std::move(gen_tree("ALERT", pkt, msg)));
  }

  void log(snort::Packet *pkt, const char *msg, Event *) override {
    get_logger().log(std::move(gen_tree("log", pkt, msg)));
  }

  LioLi::Tree gen_tree(const char *type, snort::Packet *pkt, const char *msg) {
    assert(type && pkt && msg);

    LioLi::Tree root("$");

    root << (LioLi::Tree(type) << msg);

    // format_IP_MAC handles a null flow
    root << (LioLi::Tree("principal") << LioLi::TreeGenerators::format_IP_MAC(pkt, pkt->flow, true));
    root << (LioLi::Tree("endpoint") << LioLi::TreeGenerators::format_IP_MAC(pkt, pkt->flow, false));

    if (pkt->flow && pkt->flow->service) {
      root << (LioLi::Tree("protocol") <<  pkt->flow->service);
    }

    return root;
  }

public:
  static snort::Logger *ctor(snort::Module *module) {
    return new Logger(dynamic_cast<Module *>(module));
  }

  static void dtor(snort::Logger *p) { delete dynamic_cast<Logger *>(p); }
};

} // namespace

typedef Logger *(*LogNewFunc)(class Module *);
typedef void (*LogDelFunc)(Logger *);

const snort::LogApi log_api{
    {
        PT_LOGGER,
        sizeof(snort::LogApi),
        LOGAPI_VERSION,
        0,
        API_RESERVED,
        API_OPTIONS,
        s_name,
        s_help,
        Module::ctor,
        Module::dtor,
    },
    OUTPUT_TYPE_FLAG__ALERT,
    Logger::ctor,
    Logger::dtor,
};

} // namespace alert_lioli
