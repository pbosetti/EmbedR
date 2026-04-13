#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <utility>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

struct SEXPREC;
using SEXP = SEXPREC*;

namespace EmbedR {

/**
 * @brief Embedded R interpreter wrapper with JSON and graphics helpers.
 */
class RInterpreter {
public:
  class Function;

  /**
   * @brief Output routing policy for R console text.
   */
  enum class OutputMode {
    /** @brief Forward output to process stdout/stderr. */
    Stdout,
    /** @brief Append output to internal stdout/stderr string streams owned by the instance. */
    Buffer
  };

  /**
   * @brief Supported output formats for generated plots.
   */
  enum class GraphicsFormat {
    /** @brief Render plot as PNG bytes. */
    Png,
    /** @brief Render plot as PDF bytes. */
    Pdf
  };

  /**
   * @brief Startup options for the embedded interpreter.
   */
  struct Options {
    /** @brief Optional explicit path to R home. */
    std::optional<std::filesystem::path> r_home;
    /** @brief Optional path to a `.Renviron`/`.Renv` file to load. */
    std::optional<std::filesystem::path> renv_path;
    /** @brief When true, auto-detect `.Renviron`/`.Renv` in `working_directory`. */
    bool auto_load_current_dir_renv = true;
    /** @brief Directory used when searching for environment files. */
    std::filesystem::path working_directory = std::filesystem::current_path();
    /** @brief Destination for R console output emitted during evaluations/sourcing. */
    OutputMode output_mode = OutputMode::Stdout;
  };

  /**
   * @brief Result type for evaluated R expressions.
   *
   * Base vectors/scalars are mapped to C++ scalar/container alternatives.
   * Named R lists are mapped to `nlohmann::json` objects.
   */
  using RValue = std::variant<std::nullptr_t,
                              double,
                              std::int64_t,
                              bool,
                              std::string,
                              std::vector<double>,
                              std::vector<std::int64_t>,
                              std::vector<bool>,
                              std::vector<std::string>,
                              nlohmann::json>;

  /**
   * @brief Construct using default options.
   * @throws std::runtime_error if R home cannot be resolved/validated or R initialization fails.
   */
  RInterpreter();
  /**
   * @brief Construct with explicit startup options.
   * @param options Startup configuration.
   * @throws std::runtime_error if configured paths are invalid, environment setup fails, or R initialization fails.
   */
  explicit RInterpreter(Options options);
  /**
   * @brief Construct and source an R script immediately after startup.
   * @param startup_script Path to an R script file.
   * @throws std::runtime_error if startup fails or script sourcing fails.
   */
  explicit RInterpreter(const std::filesystem::path& startup_script);
  /**
   * @brief Construct with options and source an R script immediately after startup.
   * @param options Startup configuration.
   * @param startup_script Path to an R script file.
   * @throws std::runtime_error if startup fails or script sourcing fails.
   */
  RInterpreter(Options options, const std::filesystem::path& startup_script);
  /**
   * @brief Shut down embedded R when the last instance is destroyed.
   */
  ~RInterpreter();

  RInterpreter(const RInterpreter&) = delete;
  RInterpreter& operator=(const RInterpreter&) = delete;
  RInterpreter(RInterpreter&&) = delete;
  RInterpreter& operator=(RInterpreter&&) = delete;

  /**
   * @brief Check if R home can be resolved from options/env/defaults.
   * @return True if R can be located.
   *
   * This method does not initialize R.
   */
  static bool can_find_r();

  /**
   * @brief Evaluate R code and convert result to C++/JSON.
   * @param r_code R code to parse and evaluate.
   * @return Converted result as @ref RValue.
   * @throws std::runtime_error on parse or runtime errors.
   */
  RValue eval(const std::string& r_code) const;
  /**
   * @brief Evaluate R code and force JSON conversion.
   * @param r_code R code to parse and evaluate.
   * @return Result converted to `nlohmann::json`.
   * @throws std::runtime_error on parse or runtime errors.
   */
  nlohmann::json eval_json(const std::string& r_code) const;

  /**
   * @brief Return a callable reference to an R function in the global environment.
   * @param name Function symbol to resolve.
   * @return Callable function wrapper bound to this interpreter.
   */
  Function function(std::string name) const;

  /**
   * @brief Read captured stdout buffer as a string.
   * @return Buffer contents; empty if output mode is not buffer or no output was captured.
   */
  std::string get_stdout_buffer() const;

  /**
   * @brief Read captured stderr buffer as a string.
   * @return Buffer contents; empty if output mode is not buffer or no output was captured.
   */
  std::string get_stderr_buffer() const;

  /**
   * @brief Clear captured stdout/stderr buffers.
   */
  void clear_output_buffers() const;

  /**
   * @brief Source an R script file in the global environment.
   * @param script_path Path to the script file.
   * @throws std::runtime_error if the script does not exist, is not a file, or sourcing fails.
   */
  void source_script(const std::filesystem::path& script_path) const;

  /**
   * @brief Assign a JSON value into R global environment as vectors/lists.
   * @param name Variable name in `R_GlobalEnv`.
   * @param value JSON payload to convert and assign.
   */
  void assign_json_as_list(const std::string& name, const nlohmann::json& value) const;

  /**
   * @brief Execute plotting code and return generated image/document bytes.
   * @param plot_code R plotting code.
   * @param format Output format (`Png` or `Pdf`).
   * @param width Output width in pixels.
   * @param height Output height in pixels.
   * @param dpi Resolution used for PNG and PDF size conversion.
   * @return Raw bytes of the generated file.
   * @throws std::invalid_argument if `width`, `height`, or `dpi` are non-positive.
   * @throws std::runtime_error on plotting or I/O errors.
   */
  std::vector<std::uint8_t> render_plot(const std::string& plot_code,
                                        GraphicsFormat format,
                                        int width = 800,
                                        int height = 600,
                                        int dpi = 96) const;

  /**
   * @brief Callable wrapper for an R function resolved through an interpreter instance.
   */
  class Function {
  public:
    RValue operator()(const nlohmann::json& argument) const;
    RValue operator()() const;
    nlohmann::json eval_json(const nlohmann::json& argument) const;

  private:
    friend class RInterpreter;

    Function(const RInterpreter& interpreter, std::string name);

    const RInterpreter* interpreter_;
    std::string name_;
  };

private:
  static std::filesystem::path find_r_home(const std::optional<std::filesystem::path>& configured_r_home);
  static std::optional<std::filesystem::path> find_renv_file(const Options& options);
  static void restore_renv_if_needed(const Options& options);
  static void initialize_r(const std::filesystem::path& r_home,
                           const std::optional<std::filesystem::path>& renv_file);

  static std::string escape_r_string(const std::string& input);

  static nlohmann::json sexp_to_json(SEXP sexp);
  static RValue sexp_to_value(SEXP sexp);
  static SEXP json_to_sexp(const nlohmann::json& value);

  static std::string get_last_r_error();
  static void source_script_unlocked(const std::filesystem::path& script_path);
  static SEXP eval_to_sexp(const std::string& r_code);
  static SEXP find_function_unlocked(const std::string& name);
  static void start_output_capture_unlocked();
  static std::pair<std::string, std::string> stop_output_capture_unlocked();
  void append_captured_output_unlocked(const std::pair<std::string, std::string>& captured) const;
  RValue call_function(const std::string& name, const nlohmann::json& argument) const;
  nlohmann::json call_function_json(const std::string& name, const nlohmann::json& argument) const;

  Options options_;
  mutable std::stringstream stdout_stream_;
  mutable std::stringstream stderr_stream_;

  static bool is_r_initialized_;
};

}  // namespace EmbedR
