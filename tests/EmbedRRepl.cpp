#include "EmbedR.hpp"

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

#ifdef HAVE_READLINE
#include <readline/history.h>
#include <readline/readline.h>
#endif

namespace {

using EmbedR::RInterpreter;

bool ends_with(const std::string &text, const std::string &suffix) {
  return text.size() >= suffix.size() &&
         text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool consume_continue_marker(std::string &line, const std::string &token) {
  if (!line.empty() && line.back() == '\\') {
    line.pop_back();
    return true;
  }

  if (!token.empty() && ends_with(line, token)) {
    line.erase(line.size() - token.size());
    return true;
  }

  if (ends_with(line, "\x1b[13;2u")) {
    line.erase(line.size() - 7);
    return true;
  }

  if (ends_with(line, "\x1b[27;2;13~")) {
    line.erase(line.size() - 10);
    return true;
  }

  return false;
}

std::filesystem::path default_history_file() {
  if (const char *home = std::getenv("HOME");
      home != nullptr && home[0] != '\0') {
    return std::filesystem::path(home) / ".embedr_repl_history";
  }
#ifdef _WIN32
  if (const char *userprofile = std::getenv("USERPROFILE");
      userprofile != nullptr && userprofile[0] != '\0') {
    return std::filesystem::path(userprofile) / ".embedr_repl_history";
  }
#endif
  return std::filesystem::current_path() / ".embedr_repl_history";
}

std::optional<std::filesystem::path>
detect_renv_lock(const std::filesystem::path &working_directory) {
  const auto candidate = working_directory / "renv.lock";
  if (std::filesystem::is_regular_file(candidate)) {
    return candidate;
  }
  return std::nullopt;
}

class HistorySession {
public:
  explicit HistorySession(std::filesystem::path history_path)
      : history_path_(std::move(history_path)) {
#ifdef HAVE_READLINE
    using_history();
    (void)read_history(history_path_.string().c_str());
#endif
  }

  ~HistorySession() {
#ifdef HAVE_READLINE
    try {
      (void)write_history(history_path_.string().c_str());
    } catch (...) {
    }
#endif
  }

private:
  std::filesystem::path history_path_;
};

std::string to_string_value(const RInterpreter::RValue &value) {
  return std::visit(
      [](const auto &arg) -> std::string {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, std::nullptr_t>) {
          return "null";
        } else if constexpr (std::is_same_v<T, bool>) {
          return arg ? "true" : "false";
        } else if constexpr (std::is_same_v<T, std::string>) {
          return arg;
        } else if constexpr (std::is_same_v<T, nlohmann::json>) {
          return arg.dump(2);
        } else if constexpr (std::is_same_v<T, std::vector<bool>>) {
          std::string out = "[";
          for (std::size_t i = 0; i < arg.size(); ++i) {
            if (i > 0) {
              out += ", ";
            }
            out += arg[i] ? "true" : "false";
          }
          out += "]";
          return out;
        } else if constexpr (std::is_same_v<T, std::vector<std::string>>) {
          std::string out = "[";
          for (std::size_t i = 0; i < arg.size(); ++i) {
            if (i > 0) {
              out += ", ";
            }
            out += '"' + arg[i] + '"';
          }
          out += "]";
          return out;
        } else if constexpr (std::is_same_v<T, std::vector<double>> ||
                             std::is_same_v<T, std::vector<std::int64_t>>) {
          std::string out = "[";
          for (std::size_t i = 0; i < arg.size(); ++i) {
            if (i > 0) {
              out += ", ";
            }
            out += std::to_string(arg[i]);
          }
          out += "]";
          return out;
        } else {
          return std::to_string(arg);
        }
      },
      value);
}

} // namespace

int main(int argc, char **argv) {
  bool no_renv = false;
  for (int i = 1; i < argc; ++i) {
    const std::string arg(argv[i]);
    if (arg == "--no-renv") {
      no_renv = true;
      continue;
    }

    std::cerr << "Unknown option: " << arg << "\n";
    std::cerr << "Usage: " << argv[0] << " [--no-renv]\n";
    return 1;
  }

  if (!RInterpreter::can_find_r()) {
    std::cerr << "R interpreter not found. Set R_HOME or pass "
                 "EmbedR::RInterpreter::Options::r_home.\n";
    return 1;
  }

  try {
    RInterpreter::Options options;
    options.auto_load_current_dir_renv = !no_renv;

    const auto renv_lock_file =
        options.auto_load_current_dir_renv
        ? detect_renv_lock(options.working_directory)
            : std::optional<std::filesystem::path>{};

    RInterpreter interpreter(options);
    HistorySession history(default_history_file());

    const char *env_token = std::getenv("EMBEDR_CONTINUE_TOKEN");
    const std::string continue_token =
        (env_token != nullptr && env_token[0] != '\0') ? std::string(env_token)
                                                       : std::string("\\");

    std::cout << "EmbedR REPL\n";
    std::cout << "Options: --no-renv disables .Renviron/.Renv autoload and renv.lock restore.\n";
    if (!options.auto_load_current_dir_renv) {
      std::cout << "renv restore: skipped (--no-renv)\n";
    } else if (renv_lock_file.has_value()) {
      std::cout << "renv restore: executed from " << renv_lock_file->string()
                << "\n";
    } else {
      std::cout << "renv restore: no renv.lock found in "
                << options.working_directory.string() << "\n";
    }
    std::cout << "Press Enter to execute the current block.\n";
    std::cout << "Shift+Enter newline via terminal mapping token: "
              << continue_token << "\n";
    std::cout
        << "Fallback: append \\ at end of line to continue on a new line.\n";
    std::cout << "Press Ctrl+D to exit.\n";

    std::vector<std::string> buffer;

    while (true) {
      const char *prompt = buffer.empty() ? "R> " : ">> ";
#ifdef HAVE_READLINE
      char *raw = readline(prompt);

      if (raw == nullptr) {
        std::cout << "\n";
        break;
      }

      std::string line(raw);
      std::free(raw);
#else
      std::cout << prompt << std::flush;
      std::string line;
      if (!std::getline(std::cin, line)) {
        std::cout << "\n";
        break;
      }
#endif

      const bool continue_multiline =
          consume_continue_marker(line, continue_token);

      buffer.push_back(line);
      if (continue_multiline) {
        continue;
      }

      std::string code;
      for (std::size_t i = 0; i < buffer.size(); ++i) {
        if (i > 0) {
          code.push_back('\n');
        }
        code += buffer[i];
      }
      if (!buffer.empty()) {
        code.push_back('\n');
      }
      buffer.clear();

      if (code.empty()) {
        continue;
      }

#ifdef HAVE_READLINE
      add_history(code.c_str());
#endif

      try {
        const auto result = interpreter.eval(code);
        std::cout << to_string_value(result) << "\n";
      } catch (const std::exception &ex) {
        std::cerr << ex.what() << "\n";
      }
    }

    return 0;
  } catch (const std::exception &ex) {
    std::cerr << "Unhandled exception: " << ex.what() << "\n";
    return 1;
  }
}
