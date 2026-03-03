#include "EmbedR.hpp"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

#include <readline/history.h>
#include <readline/readline.h>

namespace {

using EmbedR::RInterpreter;

std::string to_string_value(const RInterpreter::RValue& value) {
  return std::visit(
      [](const auto& arg) -> std::string {
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
        } else if constexpr (std::is_same_v<T, std::vector<double>> || std::is_same_v<T, std::vector<std::int64_t>>) {
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

}  // namespace

int main() {
  if (!RInterpreter::can_find_r()) {
    std::cerr << "R interpreter not found. Set R_HOME or pass EmbedR::RInterpreter::Options::r_home.\n";
    return 1;
  }

  try {
    RInterpreter interpreter;

    std::cout << "EmbedR REPL\n";
    std::cout << "Press Enter to execute the current block.\n";
    std::cout << "Append \\ at end of line to continue on a new line.\n";
    std::cout << "Press Ctrl+D to exit.\n";

    std::vector<std::string> buffer;

    while (true) {
      const char* prompt = buffer.empty() ? "R> " : "+ ";
      char* raw = readline(prompt);

      if (raw == nullptr) {
        std::cout << "\n";
        break;
      }

      std::string line(raw);
      std::free(raw);

      bool continue_multiline = false;
      if (!line.empty() && line.back() == '\\') {
        line.pop_back();
        continue_multiline = true;
      }

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
      code += line;
      buffer.clear();

      if (code.empty()) {
        continue;
      }

      add_history(code.c_str());

      try {
        const auto result = interpreter.eval(code);
        std::cout << to_string_value(result) << "\n";
      } catch (const std::exception& ex) {
        std::cerr << ex.what() << "\n";
      }
    }

    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "Unhandled exception: " << ex.what() << "\n";
    return 1;
  }
}
