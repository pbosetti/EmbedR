#include "EmbedR.hpp"

#include <cmath>
#include <cstdint>
#include <exception>
#include <iostream>
#include <string>
#include <variant>
#include <vector>

namespace {

using EmbedR::RInterpreter;

bool expect_true(bool value, const std::string& label) {
  if (!value) {
    std::cerr << "FAILED: " << label << '\n';
    return false;
  }
  return true;
}

bool test_scalar_eval(RInterpreter& r) {
  const auto value = r.eval("1 + 1");
  if (!std::holds_alternative<double>(value)) {
    std::cerr << "FAILED: scalar eval type mismatch\n";
    return false;
  }
  return expect_true(std::fabs(std::get<double>(value) - 2.0) < 1e-12, "scalar eval value mismatch");
}

bool test_vector_eval(RInterpreter& r) {
  const auto value = r.eval("c(1L, 2L, 3L)");
  if (!std::holds_alternative<std::vector<std::int64_t>>(value)) {
    std::cerr << "FAILED: integer vector type mismatch\n";
    return false;
  }

  const auto& vec = std::get<std::vector<std::int64_t>>(value);
  return expect_true(vec.size() == 3 && vec[0] == 1 && vec[1] == 2 && vec[2] == 3, "integer vector value mismatch");
}

bool test_list_to_json(RInterpreter& r) {
  const auto value = r.eval("list(a = 1L, b = 'x', c = list(z = TRUE))");
  if (!std::holds_alternative<nlohmann::json>(value)) {
    std::cerr << "FAILED: list to json type mismatch\n";
    return false;
  }

  const auto& js = std::get<nlohmann::json>(value);
  return expect_true(js["a"] == 1 && js["b"] == "x" && js["c"]["z"] == true, "list to json content mismatch");
}

bool test_json_to_list(RInterpreter& r) {
  const nlohmann::json input = {
      {"a", 10},
      {"b", nlohmann::json::array({1, 2, 3})},
      {"nested", {{"ok", true}}},
  };

  r.assign_json_as_list("payload", input);
  const auto value = r.eval("payload$a + payload$b[[2]]");
  if (std::holds_alternative<double>(value)) {
    return expect_true(std::fabs(std::get<double>(value) - 12.0) < 1e-12, "json assignment eval mismatch");
  }

  if (std::holds_alternative<std::int64_t>(value)) {
    return expect_true(std::get<std::int64_t>(value) == 12, "json assignment eval mismatch");
  }

  std::cerr << "FAILED: json assignment eval type mismatch\n";
  return false;
}

bool test_plot_png_and_pdf(RInterpreter& r) {
  const std::string code = "plot(1:5, 1:5, type='l')";

  const auto png = r.render_plot(code, RInterpreter::GraphicsFormat::Png, 640, 480, 96);
  const auto pdf = r.render_plot(code, RInterpreter::GraphicsFormat::Pdf, 640, 480, 96);

  const bool png_ok = png.size() > 8 && png[0] == 0x89 && png[1] == 0x50 && png[2] == 0x4E && png[3] == 0x47;
  const bool pdf_ok = pdf.size() > 4 && pdf[0] == 0x25 && pdf[1] == 0x50 && pdf[2] == 0x44 && pdf[3] == 0x46;

  return expect_true(png_ok && pdf_ok, "plot output signatures mismatch");
}

}  // namespace

int main() {
  if (!RInterpreter::can_find_r()) {
    std::cout << "R interpreter not found, skipping tests.\n";
    return 0;
  }

  try {
    RInterpreter::Options options;
    options.auto_load_current_dir_renv = true;
    RInterpreter r(options);

    bool ok = true;
    ok = test_scalar_eval(r) && ok;
    ok = test_vector_eval(r) && ok;
    ok = test_list_to_json(r) && ok;
    ok = test_json_to_list(r) && ok;
    ok = test_plot_png_and_pdf(r) && ok;

    if (!ok) {
      return 1;
    }

    std::cout << "All EmbedR tests passed.\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "Unhandled exception: " << ex.what() << '\n';
    return 1;
  }
}
