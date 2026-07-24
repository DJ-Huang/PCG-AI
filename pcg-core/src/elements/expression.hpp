#pragma once

#include "elements/color_ramp.hpp"

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

namespace pcg::internal::elements::expression {

struct EvalContext {
    std::function<bool(const std::string&, double&)> read_variable;
    std::function<bool(const std::string&, double)> write_variable;
    std::function<bool(const std::string&, std::array<double, 3>&)> read_vector;
    std::function<bool(const std::string&, const std::array<double, 3>&)> write_vector;
    std::unordered_map<std::string, double> parameters;
    std::unordered_map<std::string, double> locals;
    std::unordered_map<std::string, std::array<double, 3>> local_vectors;
    ColorRampMap ramps;
};

class Program {
public:
    Program();

    static bool compile_statements(const std::string& source, Program& out, std::string& error);
    static bool compile_expression(const std::string& source, Program& out, std::string& error);

    bool execute(EvalContext& context, std::string& error) const;
    bool evaluate(EvalContext& context, double& value, std::string& error) const;
    bool empty() const;

private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};

} // namespace pcg::internal::elements::expression
