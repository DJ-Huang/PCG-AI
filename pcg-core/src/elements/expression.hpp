#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

namespace pcg::internal::elements::expression {

struct EvalContext {
    std::function<bool(const std::string&, double&)> read_variable;
    std::function<bool(const std::string&, double)> write_variable;
    std::unordered_map<std::string, double> parameters;
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
