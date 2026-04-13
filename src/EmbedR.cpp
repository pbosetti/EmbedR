#include "EmbedR.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <mutex>
#include <random>
#include <sstream>
#include <stdexcept>
#include <system_error>

#define R_NO_REMAP
#include <Rinternals.h>
#include <R_ext/Parse.h>
#include <Rembedded.h>

#ifdef eval
#undef eval
#endif
#ifdef length
#undef length
#endif
#ifdef error
#undef error
#endif

namespace {

std::mutex g_r_mutex;
int g_instance_count = 0;

#ifdef _WIN32
void set_process_env(const std::string& key, const std::string& value) {
  if (_putenv_s(key.c_str(), value.c_str()) != 0) {
    throw std::runtime_error("Failed to set environment variable: " + key);
  }
}
#else
void set_process_env(const std::string& key, const std::string& value) {
  if (setenv(key.c_str(), value.c_str(), 1) != 0) {
    throw std::runtime_error("Failed to set environment variable: " + key);
  }
}
#endif

std::string make_temp_file_name(const std::string& extension) {
  static thread_local std::mt19937_64 rng{std::random_device{}()};
  std::uniform_int_distribution<unsigned long long> dist;
  std::ostringstream os;
  os << "r_embed_plot_" << dist(rng) << extension;
  return os.str();
}

std::filesystem::path make_temp_file_path(const std::string& extension) {
  const auto temp_dir = std::filesystem::temp_directory_path();
  for (int attempt = 0; attempt < 128; ++attempt) {
    const auto candidate = temp_dir / make_temp_file_name(extension);
    if (!std::filesystem::exists(candidate)) {
      return candidate;
    }
  }
  throw std::runtime_error("Failed to allocate unique temporary file path for plot output.");
}

SEXP json_to_sexp_internal(const nlohmann::json& value, int& protect_count) {
  if (value.is_null()) {
    return R_NilValue;
  }

  if (value.is_boolean()) {
    SEXP out = PROTECT(Rf_ScalarLogical(value.get<bool>() ? TRUE : FALSE));
    ++protect_count;
    return out;
  }

  if (value.is_number_integer()) {
    const auto number = value.get<long long>();
    if (number < std::numeric_limits<int>::min() || number > std::numeric_limits<int>::max()) {
      SEXP out = PROTECT(Rf_ScalarReal(static_cast<double>(number)));
      ++protect_count;
      return out;
    }
    SEXP out = PROTECT(Rf_ScalarInteger(static_cast<int>(number)));
    ++protect_count;
    return out;
  }

  if (value.is_number_unsigned()) {
    const auto number = value.get<unsigned long long>();
    if (number <= static_cast<unsigned long long>(std::numeric_limits<int>::max())) {
      SEXP out = PROTECT(Rf_ScalarInteger(static_cast<int>(number)));
      ++protect_count;
      return out;
    }
    SEXP out = PROTECT(Rf_ScalarReal(static_cast<double>(number)));
    ++protect_count;
    return out;
  }

  if (value.is_number_float()) {
    SEXP out = PROTECT(Rf_ScalarReal(value.get<double>()));
    ++protect_count;
    return out;
  }

  if (value.is_string()) {
    SEXP out = PROTECT(Rf_mkString(value.get_ref<const std::string&>().c_str()));
    ++protect_count;
    return out;
  }

  if (value.is_array()) {
    const bool all_bools = std::all_of(value.begin(), value.end(), [](const auto& item) { return item.is_boolean(); });
    const bool all_ints = std::all_of(value.begin(), value.end(), [](const auto& item) {
      return item.is_number_integer() || item.is_number_unsigned();
    });
    const bool all_numbers = std::all_of(value.begin(), value.end(), [](const auto& item) { return item.is_number(); });
    const bool all_strings = std::all_of(value.begin(), value.end(), [](const auto& item) { return item.is_string(); });

    if (all_bools) {
      SEXP out = PROTECT(Rf_allocVector(LGLSXP, static_cast<R_xlen_t>(value.size())));
      ++protect_count;
      for (std::size_t i = 0; i < value.size(); ++i) {
        LOGICAL(out)[i] = value[i].get<bool>() ? TRUE : FALSE;
      }
      return out;
    }

    if (all_ints) {
      SEXP out = PROTECT(Rf_allocVector(INTSXP, static_cast<R_xlen_t>(value.size())));
      ++protect_count;
      for (std::size_t i = 0; i < value.size(); ++i) {
        const auto number = value[i].is_number_unsigned() ? static_cast<long long>(value[i].get<unsigned long long>())
                                                          : value[i].get<long long>();
        if (number < std::numeric_limits<int>::min() || number > std::numeric_limits<int>::max()) {
          UNPROTECT(1);
          --protect_count;
          out = PROTECT(Rf_allocVector(REALSXP, static_cast<R_xlen_t>(value.size())));
          ++protect_count;
          for (std::size_t j = 0; j < value.size(); ++j) {
            REAL(out)[j] = value[j].is_number_unsigned() ? static_cast<double>(value[j].get<unsigned long long>())
                                                         : static_cast<double>(value[j].get<long long>());
          }
          return out;
        }
        INTEGER(out)[i] = static_cast<int>(number);
      }
      return out;
    }

    if (all_numbers) {
      SEXP out = PROTECT(Rf_allocVector(REALSXP, static_cast<R_xlen_t>(value.size())));
      ++protect_count;
      for (std::size_t i = 0; i < value.size(); ++i) {
        REAL(out)[i] = value[i].get<double>();
      }
      return out;
    }

    if (all_strings) {
      SEXP out = PROTECT(Rf_allocVector(STRSXP, static_cast<R_xlen_t>(value.size())));
      ++protect_count;
      for (std::size_t i = 0; i < value.size(); ++i) {
        SET_STRING_ELT(out, i, Rf_mkChar(value[i].get_ref<const std::string&>().c_str()));
      }
      return out;
    }

    SEXP out = PROTECT(Rf_allocVector(VECSXP, static_cast<R_xlen_t>(value.size())));
    ++protect_count;
    for (std::size_t i = 0; i < value.size(); ++i) {
      SET_VECTOR_ELT(out, i, json_to_sexp_internal(value[i], protect_count));
    }
    return out;
  }

  SEXP out = PROTECT(Rf_allocVector(VECSXP, static_cast<R_xlen_t>(value.size())));
  ++protect_count;
  SEXP names = PROTECT(Rf_allocVector(STRSXP, static_cast<R_xlen_t>(value.size())));
  ++protect_count;

  std::size_t index = 0;
  for (auto it = value.begin(); it != value.end(); ++it, ++index) {
    SET_VECTOR_ELT(out, index, json_to_sexp_internal(it.value(), protect_count));
    SET_STRING_ELT(names, index, Rf_mkChar(it.key().c_str()));
  }
  Rf_setAttrib(out, R_NamesSymbol, names);
  return out;
}

}  // namespace

namespace EmbedR {

bool RInterpreter::is_r_initialized_ = false;

RInterpreter::RInterpreter() : RInterpreter(Options{}) {
}

RInterpreter::RInterpreter(Options options) : options_(std::move(options)) {
  std::lock_guard<std::mutex> lock(g_r_mutex);

  const auto r_home = find_r_home(options_.r_home);
  const auto renv_file = find_renv_file(options_);

  bool initialized_here = false;
  if (!is_r_initialized_) {
    initialize_r(r_home, renv_file);
    is_r_initialized_ = true;
    initialized_here = true;
  }

  try {
    if (options_.output_mode == OutputMode::Buffer) {
      start_output_capture_unlocked();
    }
    restore_renv_if_needed(options_);
    if (options_.output_mode == OutputMode::Buffer) {
      append_captured_output_unlocked(stop_output_capture_unlocked());
    }
    ++g_instance_count;
  } catch (...) {
    if (options_.output_mode == OutputMode::Buffer) {
      try {
        append_captured_output_unlocked(stop_output_capture_unlocked());
      } catch (...) {
      }
    }
    if (initialized_here) {
      Rf_endEmbeddedR(0);
      is_r_initialized_ = false;
    }
    throw;
  }
}

RInterpreter::RInterpreter(const std::filesystem::path& startup_script) : RInterpreter(Options{}, startup_script) {
}

RInterpreter::RInterpreter(Options options, const std::filesystem::path& startup_script) : options_(std::move(options)) {
  std::lock_guard<std::mutex> lock(g_r_mutex);

  const auto r_home = find_r_home(options_.r_home);
  const auto renv_file = find_renv_file(options_);

  bool initialized_here = false;
  if (!is_r_initialized_) {
    initialize_r(r_home, renv_file);
    is_r_initialized_ = true;
    initialized_here = true;
  }
  try {
    if (options_.output_mode == OutputMode::Buffer) {
      start_output_capture_unlocked();
    }
    restore_renv_if_needed(options_);
    source_script_unlocked(startup_script);
    if (options_.output_mode == OutputMode::Buffer) {
      append_captured_output_unlocked(stop_output_capture_unlocked());
    }
    ++g_instance_count;
  } catch (...) {
    if (options_.output_mode == OutputMode::Buffer) {
      try {
        append_captured_output_unlocked(stop_output_capture_unlocked());
      } catch (...) {
      }
    }
    if (initialized_here) {
      Rf_endEmbeddedR(0);
      is_r_initialized_ = false;
    }
    throw;
  }
}

RInterpreter::~RInterpreter() {
  std::lock_guard<std::mutex> lock(g_r_mutex);
  if (g_instance_count > 0) {
    --g_instance_count;
  }

  if (g_instance_count == 0 && is_r_initialized_) {
    try {
      Rf_endEmbeddedR(0);
    } catch (...) {
      // On Windows R's shutdown may fail to remove locked temp files.
    }
    is_r_initialized_ = false;
  }
}

bool RInterpreter::can_find_r() {
  try {
    (void)find_r_home(std::nullopt);
    return true;
  } catch (...) {
    return false;
  }
}

RInterpreter::RValue RInterpreter::eval(const std::string& r_code) const {
  std::lock_guard<std::mutex> lock(g_r_mutex);

  if (options_.output_mode == OutputMode::Buffer) {
    start_output_capture_unlocked();
  }
  try {
    SEXP result = eval_to_sexp(r_code);
    PROTECT(result);
    const auto converted = sexp_to_value(result);
    UNPROTECT(1);
    if (options_.output_mode == OutputMode::Buffer) {
      append_captured_output_unlocked(stop_output_capture_unlocked());
    }
    return converted;
  } catch (...) {
    if (options_.output_mode == OutputMode::Buffer) {
      try {
        append_captured_output_unlocked(stop_output_capture_unlocked());
      } catch (...) {
      }
    }
    throw;
  }
}

nlohmann::json RInterpreter::eval_json(const std::string& r_code) const {
  std::lock_guard<std::mutex> lock(g_r_mutex);

  if (options_.output_mode == OutputMode::Buffer) {
    start_output_capture_unlocked();
  }
  try {
    SEXP result = eval_to_sexp(r_code);
    PROTECT(result);
    const auto converted = sexp_to_json(result);
    UNPROTECT(1);
    if (options_.output_mode == OutputMode::Buffer) {
      append_captured_output_unlocked(stop_output_capture_unlocked());
    }
    return converted;
  } catch (...) {
    if (options_.output_mode == OutputMode::Buffer) {
      try {
        append_captured_output_unlocked(stop_output_capture_unlocked());
      } catch (...) {
      }
    }
    throw;
  }
}

RInterpreter::Function RInterpreter::function(std::string name) const {
  return Function(*this, std::move(name));
}

std::string RInterpreter::get_stdout_buffer() const {
  std::lock_guard<std::mutex> lock(g_r_mutex);
  return stdout_stream_.str();
}

std::string RInterpreter::get_stderr_buffer() const {
  std::lock_guard<std::mutex> lock(g_r_mutex);
  return stderr_stream_.str();
}

void RInterpreter::clear_output_buffers() const {
  std::lock_guard<std::mutex> lock(g_r_mutex);
  stdout_stream_.str("");
  stdout_stream_.clear();
  stderr_stream_.str("");
  stderr_stream_.clear();
}

void RInterpreter::source_script(const std::filesystem::path& script_path) const {
  std::lock_guard<std::mutex> lock(g_r_mutex);
  if (options_.output_mode == OutputMode::Buffer) {
    start_output_capture_unlocked();
  }
  try {
    source_script_unlocked(script_path);
    if (options_.output_mode == OutputMode::Buffer) {
      append_captured_output_unlocked(stop_output_capture_unlocked());
    }
  } catch (...) {
    if (options_.output_mode == OutputMode::Buffer) {
      try {
        append_captured_output_unlocked(stop_output_capture_unlocked());
      } catch (...) {
      }
    }
    throw;
  }
}

void RInterpreter::assign_json_as_list(const std::string& name, const nlohmann::json& value) const {
  std::lock_guard<std::mutex> lock(g_r_mutex);
  int protect_count = 0;
  SEXP r_value = json_to_sexp_internal(value, protect_count);
  Rf_defineVar(Rf_install(name.c_str()), r_value, R_GlobalEnv);
  UNPROTECT(protect_count);
}

RInterpreter::RValue RInterpreter::call_function(const std::string& name, const nlohmann::json& argument) const {
  std::lock_guard<std::mutex> lock(g_r_mutex);

  if (options_.output_mode == OutputMode::Buffer) {
    start_output_capture_unlocked();
  }
  try {
    int protect_count = 0;
    (void)find_function_unlocked(name);
    SEXP call;
    if (argument.is_null()) {
      call = PROTECT(Rf_lang1(Rf_install(name.c_str())));
    } else {
      SEXP r_argument = json_to_sexp_internal(argument, protect_count);
      call = PROTECT(Rf_lang2(Rf_install(name.c_str()), r_argument));
    }
    ++protect_count;

    int error = 0;
    SEXP result = R_tryEval(call, R_GlobalEnv, &error);
    if (error) {
      const auto message = get_last_r_error();
      UNPROTECT(protect_count);
      throw std::runtime_error("R function call error for '" + name + "': " + message);
    }

    PROTECT(result);
    ++protect_count;
    const auto converted = sexp_to_value(result);
    UNPROTECT(protect_count);

    if (options_.output_mode == OutputMode::Buffer) {
      append_captured_output_unlocked(stop_output_capture_unlocked());
    }
    return converted;
  } catch (...) {
    if (options_.output_mode == OutputMode::Buffer) {
      try {
        append_captured_output_unlocked(stop_output_capture_unlocked());
      } catch (...) {
      }
    }
    throw;
  }
}

nlohmann::json RInterpreter::call_function_json(const std::string& name, const nlohmann::json& argument) const {
  std::lock_guard<std::mutex> lock(g_r_mutex);

  if (options_.output_mode == OutputMode::Buffer) {
    start_output_capture_unlocked();
  }
  try {
    int protect_count = 0;
    (void)find_function_unlocked(name);
    SEXP r_argument = json_to_sexp_internal(argument, protect_count);
    SEXP call = PROTECT(Rf_lang2(Rf_install(name.c_str()), r_argument));
    ++protect_count;

    int error = 0;
    SEXP result = R_tryEval(call, R_GlobalEnv, &error);
    if (error) {
      const auto message = get_last_r_error();
      UNPROTECT(protect_count);
      throw std::runtime_error("R function call error for '" + name + "': " + message);
    }

    PROTECT(result);
    ++protect_count;
    const auto converted = sexp_to_json(result);
    UNPROTECT(protect_count);

    if (options_.output_mode == OutputMode::Buffer) {
      append_captured_output_unlocked(stop_output_capture_unlocked());
    }
    return converted;
  } catch (...) {
    if (options_.output_mode == OutputMode::Buffer) {
      try {
        append_captured_output_unlocked(stop_output_capture_unlocked());
      } catch (...) {
      }
    }
    throw;
  }
}

std::vector<std::uint8_t> RInterpreter::render_plot(const std::string& plot_code,
                                                    GraphicsFormat format,
                                                    int width,
                                                    int height,
                                                    int dpi) const {
  if (width <= 0 || height <= 0 || dpi <= 0) {
    throw std::invalid_argument("width, height and dpi must be positive.");
  }

  const auto extension = (format == GraphicsFormat::Png) ? ".png" : ".pdf";
  const auto temp_path = make_temp_file_path(extension);

  const auto escaped_path = escape_r_string(temp_path.string());
  if (format == GraphicsFormat::Png) {
    eval("png(filename='" + escaped_path + "', width=" + std::to_string(width) + ", height=" +
         std::to_string(height) + ", units='px', res=" + std::to_string(dpi) + ")");
  } else {
    const double width_in = static_cast<double>(width) / static_cast<double>(dpi);
    const double height_in = static_cast<double>(height) / static_cast<double>(dpi);
    eval("pdf(file='" + escaped_path + "', width=" + std::to_string(width_in) + ", height=" +
         std::to_string(height_in) + ")");
  }

  try {
    eval(plot_code);
    eval("dev.off()");
  } catch (...) {
    try {
      eval("if (dev.cur() > 1) dev.off()");
    } catch (...) {
    }
    std::error_code ec;
    std::filesystem::remove(temp_path, ec);
    throw;
  }

  std::ifstream file(temp_path, std::ios::binary);
  if (!file.good()) {
    throw std::runtime_error("Failed to read generated plot file: " + temp_path.string());
  }

  const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  file.close();
  std::error_code ec;
  std::filesystem::remove(temp_path, ec);
  return bytes;
}

std::filesystem::path RInterpreter::find_r_home(const std::optional<std::filesystem::path>& configured_r_home) {
  if (configured_r_home.has_value()) {
    if (!std::filesystem::exists(configured_r_home.value())) {
      throw std::runtime_error("Configured R home does not exist: " + configured_r_home->string());
    }
    return configured_r_home.value();
  }

  if (const char* env = std::getenv("R_HOME"); env != nullptr && std::strlen(env) > 0) {
    const std::filesystem::path path(env);
    if (std::filesystem::exists(path)) {
      return path;
    }
  }

#ifdef R_EMBED_DEFAULT_R_HOME
  if constexpr (sizeof(R_EMBED_DEFAULT_R_HOME) > 1) {
    const std::filesystem::path path(R_EMBED_DEFAULT_R_HOME);
    if (std::filesystem::exists(path)) {
      return path;
    }
  }
#endif

  throw std::runtime_error("Unable to locate R home. Provide Options::r_home or set R_HOME.");
}

std::optional<std::filesystem::path> RInterpreter::find_renv_file(const Options& options) {
  if (options.renv_path.has_value()) {
    if (!std::filesystem::exists(options.renv_path.value())) {
      throw std::runtime_error("Configured renv path does not exist: " + options.renv_path->string());
    }
    return options.renv_path.value();
  }

  if (!options.auto_load_current_dir_renv) {
    return std::nullopt;
  }

  const auto candidate_renviron = options.working_directory / ".Renviron";
  if (std::filesystem::is_regular_file(candidate_renviron)) {
    return candidate_renviron;
  }

  const auto candidate_renv = options.working_directory / ".Renv";
  if (std::filesystem::is_regular_file(candidate_renv)) {
    return candidate_renv;
  }

  const auto candidate_renv_plain = options.working_directory / "renv";
  if (std::filesystem::is_regular_file(candidate_renv_plain)) {
    return candidate_renv_plain;
  }

  if (std::filesystem::is_directory(candidate_renv_plain)) {
    const auto nested_renviron = candidate_renv_plain / ".Renviron";
    if (std::filesystem::is_regular_file(nested_renviron)) {
      return nested_renviron;
    }

    const auto nested_renv = candidate_renv_plain / ".Renv";
    if (std::filesystem::is_regular_file(nested_renv)) {
      return nested_renv;
    }
  }

  return std::nullopt;
}

void RInterpreter::restore_renv_if_needed(const Options& options) {
  if (!options.auto_load_current_dir_renv) {
    return;
  }

  const auto renv_lock = options.working_directory / "renv.lock";
  if (!std::filesystem::is_regular_file(renv_lock)) {
    return;
  }

  const auto escaped_project_path = escape_r_string(options.working_directory.string());
  (void)eval_to_sexp(
      "if (!requireNamespace('renv', quietly=TRUE)) stop(\"renv.lock found but package 'renv' is not installed\"); "
      "renv::restore(project='" +
      escaped_project_path + "', prompt=FALSE)");
}

void RInterpreter::initialize_r(const std::filesystem::path& r_home,
                                const std::optional<std::filesystem::path>& renv_file) {
  set_process_env("R_HOME", r_home.string());
  if (renv_file.has_value()) {
    set_process_env("R_ENVIRON", renv_file->string());
  }
#ifdef _WIN32
  if (const char* r_user = std::getenv("R_USER"); r_user == nullptr || std::strlen(r_user) == 0) {
    set_process_env("R_USER", std::filesystem::current_path().string());
  }
#endif

  char arg0[] = "R";
  char arg1[] = "--silent";
  char arg2[] = "--no-save";
  char arg3[] = "--vanilla";
  char* argv[] = {arg0, arg1, arg2, arg3};

  if (!std::filesystem::exists(r_home / "library" / "base")) {
    throw std::runtime_error("\nInvalid R home, missing base package under " + r_home.string() + "\nMake sure the path points to a valid R installation," + "\nsetting R_HOME=\"$(R RHOME)\" should fix this.");
  }

  Rf_initEmbeddedR(4, argv);
}

std::string RInterpreter::escape_r_string(const std::string& input) {
  std::string out;
  out.reserve(input.size());
  for (const auto c : input) {
    if (c == '\\' || c == '\'') {
      out.push_back('\\');
    }
    out.push_back(c);
  }
  return out;
}

nlohmann::json RInterpreter::sexp_to_json(SEXP value) {
  if (value == R_NilValue || TYPEOF(value) == NILSXP) {
    return nullptr;
  }

  if (TYPEOF(value) == LGLSXP) {
    if (Rf_length(value) == 1) {
      return static_cast<bool>(LOGICAL(value)[0] == TRUE);
    }
    nlohmann::json out = nlohmann::json::array();
    for (R_xlen_t i = 0; i < XLENGTH(value); ++i) {
      out.push_back(LOGICAL(value)[i] == TRUE);
    }
    return out;
  }

  if (TYPEOF(value) == INTSXP) {
    if (Rf_length(value) == 1) {
      return static_cast<std::int64_t>(INTEGER(value)[0]);
    }
    nlohmann::json out = nlohmann::json::array();
    for (R_xlen_t i = 0; i < XLENGTH(value); ++i) {
      out.push_back(static_cast<std::int64_t>(INTEGER(value)[i]));
    }
    return out;
  }

  if (TYPEOF(value) == REALSXP) {
    if (Rf_length(value) == 1) {
      return REAL(value)[0];
    }
    nlohmann::json out = nlohmann::json::array();
    for (R_xlen_t i = 0; i < XLENGTH(value); ++i) {
      out.push_back(REAL(value)[i]);
    }
    return out;
  }

  if (TYPEOF(value) == STRSXP) {
    if (Rf_length(value) == 1) {
      SEXP s = STRING_ELT(value, 0);
      return std::string(CHAR(s));
    }
    nlohmann::json out = nlohmann::json::array();
    for (R_xlen_t i = 0; i < XLENGTH(value); ++i) {
      out.push_back(std::string(CHAR(STRING_ELT(value, i))));
    }
    return out;
  }

  if (TYPEOF(value) == VECSXP || TYPEOF(value) == LISTSXP) {
    SEXP names = Rf_getAttrib(value, R_NamesSymbol);
    const bool has_names = names != R_NilValue && XLENGTH(names) == XLENGTH(value);

    if (!has_names) {
      nlohmann::json out = nlohmann::json::array();
      for (R_xlen_t i = 0; i < XLENGTH(value); ++i) {
        out.push_back(sexp_to_json(VECTOR_ELT(value, i)));
      }
      return out;
    }

    nlohmann::json out = nlohmann::json::object();
    for (R_xlen_t i = 0; i < XLENGTH(value); ++i) {
      out[std::string(CHAR(STRING_ELT(names, i)))] = sexp_to_json(VECTOR_ELT(value, i));
    }
    return out;
  }

  throw std::runtime_error("Unsupported R object type for JSON conversion: " + std::to_string(TYPEOF(value)));
}

RInterpreter::RValue RInterpreter::sexp_to_value(SEXP value) {
  if (value == R_NilValue || TYPEOF(value) == NILSXP) {
    return nullptr;
  }

  if (TYPEOF(value) == LGLSXP) {
    if (Rf_length(value) == 1) {
      return static_cast<bool>(LOGICAL(value)[0] == TRUE);
    }
    std::vector<bool> out;
    out.reserve(static_cast<std::size_t>(XLENGTH(value)));
    for (R_xlen_t i = 0; i < XLENGTH(value); ++i) {
      out.push_back(LOGICAL(value)[i] == TRUE);
    }
    return out;
  }

  if (TYPEOF(value) == INTSXP) {
    if (Rf_length(value) == 1) {
      return static_cast<std::int64_t>(INTEGER(value)[0]);
    }
    std::vector<std::int64_t> out;
    out.reserve(static_cast<std::size_t>(XLENGTH(value)));
    for (R_xlen_t i = 0; i < XLENGTH(value); ++i) {
      out.push_back(static_cast<std::int64_t>(INTEGER(value)[i]));
    }
    return out;
  }

  if (TYPEOF(value) == REALSXP) {
    if (Rf_length(value) == 1) {
      return REAL(value)[0];
    }
    std::vector<double> out;
    out.reserve(static_cast<std::size_t>(XLENGTH(value)));
    for (R_xlen_t i = 0; i < XLENGTH(value); ++i) {
      out.push_back(REAL(value)[i]);
    }
    return out;
  }

  if (TYPEOF(value) == STRSXP) {
    if (Rf_length(value) == 1) {
      return std::string(CHAR(STRING_ELT(value, 0)));
    }
    std::vector<std::string> out;
    out.reserve(static_cast<std::size_t>(XLENGTH(value)));
    for (R_xlen_t i = 0; i < XLENGTH(value); ++i) {
      out.emplace_back(CHAR(STRING_ELT(value, i)));
    }
    return out;
  }

  if (TYPEOF(value) == VECSXP || TYPEOF(value) == LISTSXP) {
    return sexp_to_json(value);
  }

  throw std::runtime_error("Unsupported R object type for conversion: " + std::to_string(TYPEOF(value)));
}

SEXP RInterpreter::json_to_sexp(const nlohmann::json& value) {
  int protect_count = 0;
  SEXP out = json_to_sexp_internal(value, protect_count);
  UNPROTECT(protect_count);
  return out;
}

void RInterpreter::start_output_capture_unlocked() {
  (void)eval_to_sexp(".EmbedR__stdout_buf <- character()");
  (void)eval_to_sexp(".EmbedR__stderr_buf <- character()");
  (void)eval_to_sexp(".EmbedR__stdout_con <- textConnection('.EmbedR__stdout_buf', open='w', local=FALSE)");
  (void)eval_to_sexp("sink(.EmbedR__stdout_con, type='output')");
  (void)eval_to_sexp(".EmbedR__stderr_con <- textConnection('.EmbedR__stderr_buf', open='w', local=FALSE)");
  (void)eval_to_sexp("sink(.EmbedR__stderr_con, type='message')");
}

std::pair<std::string, std::string> RInterpreter::stop_output_capture_unlocked() {
  (void)eval_to_sexp("sink(type='message')");
  (void)eval_to_sexp("close(.EmbedR__stderr_con)");
  (void)eval_to_sexp("sink(type='output')");
  (void)eval_to_sexp("close(.EmbedR__stdout_con)");

  const auto json_to_text = [](const nlohmann::json& value) -> std::string {
    if (value.is_string()) {
      return value.get<std::string>();
    }
    if (!value.is_array()) {
      return {};
    }

    std::string out;
    bool first = true;
    for (const auto& item : value) {
      if (!first) {
        out.push_back('\n');
      }
      first = false;
      if (item.is_string()) {
        out += item.get<std::string>();
      } else {
        out += item.dump();
      }
    }
    return out;
  };

  SEXP stdout_value = eval_to_sexp(".EmbedR__stdout_buf");
  PROTECT(stdout_value);
  const auto stdout_json = sexp_to_json(stdout_value);
  UNPROTECT(1);

  SEXP stderr_value = eval_to_sexp(".EmbedR__stderr_buf");
  PROTECT(stderr_value);
  const auto stderr_json = sexp_to_json(stderr_value);
  UNPROTECT(1);

  (void)eval_to_sexp("rm(.EmbedR__stdout_buf, .EmbedR__stderr_buf, .EmbedR__stdout_con, .EmbedR__stderr_con, inherits=TRUE)");
  return {json_to_text(stdout_json), json_to_text(stderr_json)};
}

void RInterpreter::append_captured_output_unlocked(const std::pair<std::string, std::string>& captured) const {
  if (!captured.first.empty()) {
    stdout_stream_ << captured.first;
    if (captured.first.back() != '\n') {
      stdout_stream_ << '\n';
    }
  }
  if (!captured.second.empty()) {
    stderr_stream_ << captured.second;
    if (captured.second.back() != '\n') {
      stderr_stream_ << '\n';
    }
  }
}

std::string RInterpreter::get_last_r_error() {
  ParseStatus status = PARSE_NULL;
  int error = 0;
  SEXP text = PROTECT(Rf_mkString("geterrmessage()"));
  SEXP expr = PROTECT(R_ParseVector(text, -1, &status, R_NilValue));
  std::string message = "Unknown R error";
  if (status == PARSE_OK && XLENGTH(expr) > 0) {
    SEXP err_obj = R_tryEval(VECTOR_ELT(expr, 0), R_GlobalEnv, &error);
    if (!error && TYPEOF(err_obj) == STRSXP && XLENGTH(err_obj) > 0) {
      message = CHAR(STRING_ELT(err_obj, 0));
    }
  }
  UNPROTECT(2);
  return message;
}

SEXP RInterpreter::find_function_unlocked(const std::string& name) {
  SEXP symbol = Rf_install(name.c_str());
  SEXP function_ref = Rf_findVarInFrame(R_GlobalEnv, symbol);
  if (function_ref == R_UnboundValue) {
    throw std::runtime_error("R function not found: " + name);
  }
  const int type = TYPEOF(function_ref);
  if (type != CLOSXP && type != BUILTINSXP && type != SPECIALSXP) {
    throw std::runtime_error("R symbol is not callable: " + name);
  }
  return function_ref;
}

void RInterpreter::source_script_unlocked(const std::filesystem::path& script_path) {
  if (!std::filesystem::exists(script_path)) {
    throw std::runtime_error("Script path does not exist: " + script_path.string());
  }
  if (!std::filesystem::is_regular_file(script_path)) {
    throw std::runtime_error("Script path is not a regular file: " + script_path.string());
  }

  std::ifstream file(script_path);
  if (!file.is_open()) {
    throw std::runtime_error("Failed to open script: " + script_path.string());
  }

  const std::string script_text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  try {
    (void)eval_to_sexp(script_text);
  } catch (const std::exception& ex) {
    throw std::runtime_error("Failed to source script '" + script_path.string() + "': " + ex.what());
  }
}

SEXP RInterpreter::eval_to_sexp(const std::string& r_code) {
  ParseStatus status = PARSE_NULL;
  int error = 0;

  SEXP text = PROTECT(Rf_mkString(r_code.c_str()));
  SEXP expr = PROTECT(R_ParseVector(text, -1, &status, R_NilValue));

  if (status != PARSE_OK) {
    UNPROTECT(2);
    throw std::runtime_error("Failed to parse R code.");
  }

  SEXP result = R_NilValue;
  for (R_xlen_t i = 0; i < XLENGTH(expr); ++i) {
    result = R_tryEval(VECTOR_ELT(expr, i), R_GlobalEnv, &error);
    if (error) {
      const auto message = get_last_r_error();
      UNPROTECT(2);
      throw std::runtime_error("R evaluation error: " + message);
    }
  }

  UNPROTECT(2);
  return result;
}

RInterpreter::Function::Function(const RInterpreter& interpreter, std::string name)
    : interpreter_(&interpreter), name_(std::move(name)) {
}

RInterpreter::RValue RInterpreter::Function::operator()(const nlohmann::json& argument) const {
  return interpreter_->call_function(name_, argument);
}

RInterpreter::RValue RInterpreter::Function::operator()() const {
  return interpreter_->call_function(name_, nlohmann::json());
}

nlohmann::json RInterpreter::Function::eval_json(const nlohmann::json& argument) const {
  return interpreter_->call_function_json(name_, argument);
}

}  // namespace EmbedR
