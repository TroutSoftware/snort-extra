#ifndef ouput_to_77e07bbd
#define ouput_to_77e07bbd

// Snort includes

// System includes
#include <atomic>
#include <memory>
#include <mutex>
#include <string>

// Local includes

namespace LioLi {

class LogStream : public std::enable_shared_from_this<LogStream> {
public:
  // Virtual functions

  virtual ~LogStream() = default;

  virtual void set_binary_mode() = 0;

  virtual void operator<<(const std::string &&tree) = 0;

  // Non virtual functions

  std::shared_ptr<LogStream> get_log_stream() { return shared_from_this(); }

  bool operator==(LogStream &rhs) { return (this == &rhs); }

  operator bool() const { return (this != get_null_log_stream().get()); }

  static std::shared_ptr<LogStream> get_null_log_stream();
};

class LogStreamHelper {
  std::mutex mutex;
  std::string stream_name;
  std::atomic<std::shared_ptr<LogStream>> log_stream;

public:
  void set_name(std::string &name);

  LogStream &get();
};

} // namespace LioLi

#endif