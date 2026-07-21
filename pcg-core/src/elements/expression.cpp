#include "elements/expression.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <limits>
#include <memory>
#include <sstream>
#include <utility>
#include <vector>

namespace pcg::internal::elements::expression {
namespace {

enum class TokenKind {
    End,
    Number,
    Identifier,
    String,
    LParen,
    RParen,
    Comma,
    Semicolon,
    Operator,
};

struct Token {
    TokenKind kind = TokenKind::End;
    std::string text;
    double number = 0.0;
    size_t offset = 0;
};

class Lexer {
public:
    explicit Lexer(const std::string& source) : source_(source) {}

    Token next()
    {
        skip_space_and_comments();
        if (offset_ >= source_.size())
            return {TokenKind::End, {}, 0.0, offset_};

        const size_t start = offset_;
        const char c = source_[offset_];
        if (std::isdigit(static_cast<unsigned char>(c)) ||
            (c == '.' && offset_ + 1 < source_.size() &&
             std::isdigit(static_cast<unsigned char>(source_[offset_ + 1])))) {
            char* end = nullptr;
            const double value = std::strtod(source_.c_str() + offset_, &end);
            offset_ = static_cast<size_t>(end - source_.c_str());
            return {TokenKind::Number, source_.substr(start, offset_ - start), value, start};
        }

        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '@') {
            ++offset_;
            while (offset_ < source_.size()) {
                const char ch = source_[offset_];
                if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '_' && ch != '.' &&
                    ch != '@')
                    break;
                ++offset_;
            }
            return {TokenKind::Identifier, source_.substr(start, offset_ - start), 0.0, start};
        }

        if (c == '"' || c == '\'') {
            const char quote = c;
            ++offset_;
            std::string value;
            while (offset_ < source_.size() && source_[offset_] != quote) {
                char ch = source_[offset_++];
                if (ch == '\\' && offset_ < source_.size()) {
                    const char escaped = source_[offset_++];
                    if (escaped == 'n') ch = '\n';
                    else if (escaped == 't') ch = '\t';
                    else ch = escaped;
                }
                value.push_back(ch);
            }
            if (offset_ < source_.size())
                ++offset_;
            return {TokenKind::String, std::move(value), 0.0, start};
        }

        ++offset_;
        switch (c) {
        case '(': return {TokenKind::LParen, "(", 0.0, start};
        case ')': return {TokenKind::RParen, ")", 0.0, start};
        case ',': return {TokenKind::Comma, ",", 0.0, start};
        case ';': return {TokenKind::Semicolon, ";", 0.0, start};
        default: break;
        }

        std::string op(1, c);
        if (offset_ < source_.size()) {
            const std::string pair = source_.substr(start, 2);
            if (pair == "+=" || pair == "-=" || pair == "*=" || pair == "/=" ||
                pair == "%=" || pair == "==" || pair == "!=" || pair == "<=" ||
                pair == ">=" || pair == "&&" || pair == "||") {
                op = pair;
                ++offset_;
            }
        }
        return {TokenKind::Operator, std::move(op), 0.0, start};
    }

private:
    void skip_space_and_comments()
    {
        while (offset_ < source_.size()) {
            if (std::isspace(static_cast<unsigned char>(source_[offset_]))) {
                ++offset_;
                continue;
            }
            if (source_[offset_] == '/' && offset_ + 1 < source_.size() &&
                source_[offset_ + 1] == '/') {
                offset_ += 2;
                while (offset_ < source_.size() && source_[offset_] != '\n')
                    ++offset_;
                continue;
            }
            break;
        }
    }

    const std::string& source_;
    size_t offset_ = 0;
};

struct Value {
    enum class Kind { Number, String } kind = Kind::Number;
    double number = 0.0;
    std::string text;

    static Value numeric(double value) { return {Kind::Number, value, {}}; }
    static Value string(std::string value) { return {Kind::String, 0.0, std::move(value)}; }
};

struct Expr {
    enum class Kind { Number, String, Variable, Unary, Binary, Function } kind = Kind::Number;
    double number = 0.0;
    std::string text;
    std::vector<std::unique_ptr<Expr>> args;
};

struct Statement {
    std::string target;
    std::string op;
    std::unique_ptr<Expr> value;
};

class Parser {
public:
    explicit Parser(const std::string& source) : lexer_(source) { advance(); }

    bool parse_statements(std::vector<Statement>& statements, std::string& error)
    {
        while (current_.kind != TokenKind::End) {
            if (current_.kind == TokenKind::Semicolon) {
                advance();
                continue;
            }

            if (current_.kind == TokenKind::Identifier &&
                (current_.text == "float" || current_.text == "int" || current_.text == "f"))
                advance();

            if (current_.kind != TokenKind::Identifier || current_.text.empty() ||
                current_.text.front() != '@')
                return fail(error, "expected an @attribute assignment target");
            Statement statement;
            statement.target = current_.text;
            advance();
            if (current_.kind != TokenKind::Operator ||
                (current_.text != "=" && current_.text != "+=" && current_.text != "-=" &&
                 current_.text != "*=" && current_.text != "/=" && current_.text != "%="))
                return fail(error, "expected assignment operator");
            statement.op = current_.text;
            advance();
            statement.value = parse_logical_or(error);
            if (!statement.value)
                return false;
            statements.push_back(std::move(statement));
            if (current_.kind == TokenKind::Semicolon)
                advance();
            else if (current_.kind != TokenKind::End)
                return fail(error, "expected ';' between assignments");
        }
        return true;
    }

    std::unique_ptr<Expr> parse_single_expression(std::string& error)
    {
        auto expression = parse_logical_or(error);
        if (!expression)
            return nullptr;
        if (current_.kind == TokenKind::Semicolon)
            advance();
        if (current_.kind != TokenKind::End) {
            fail(error, "unexpected token after expression");
            return nullptr;
        }
        return expression;
    }

private:
    std::unique_ptr<Expr> parse_logical_or(std::string& error)
    {
        auto left = parse_logical_and(error);
        while (left && is_operator("||")) {
            const std::string op = current_.text;
            advance();
            auto right = parse_logical_and(error);
            if (!right) return nullptr;
            left = binary(op, std::move(left), std::move(right));
        }
        return left;
    }

    std::unique_ptr<Expr> parse_logical_and(std::string& error)
    {
        auto left = parse_equality(error);
        while (left && is_operator("&&")) {
            const std::string op = current_.text;
            advance();
            auto right = parse_equality(error);
            if (!right) return nullptr;
            left = binary(op, std::move(left), std::move(right));
        }
        return left;
    }

    std::unique_ptr<Expr> parse_equality(std::string& error)
    {
        auto left = parse_comparison(error);
        while (left && (is_operator("==") || is_operator("!="))) {
            const std::string op = current_.text;
            advance();
            auto right = parse_comparison(error);
            if (!right) return nullptr;
            left = binary(op, std::move(left), std::move(right));
        }
        return left;
    }

    std::unique_ptr<Expr> parse_comparison(std::string& error)
    {
        auto left = parse_term(error);
        while (left && (is_operator("<") || is_operator("<=") || is_operator(">") ||
                        is_operator(">="))) {
            const std::string op = current_.text;
            advance();
            auto right = parse_term(error);
            if (!right) return nullptr;
            left = binary(op, std::move(left), std::move(right));
        }
        return left;
    }

    std::unique_ptr<Expr> parse_term(std::string& error)
    {
        auto left = parse_factor(error);
        while (left && (is_operator("+") || is_operator("-"))) {
            const std::string op = current_.text;
            advance();
            auto right = parse_factor(error);
            if (!right) return nullptr;
            left = binary(op, std::move(left), std::move(right));
        }
        return left;
    }

    std::unique_ptr<Expr> parse_factor(std::string& error)
    {
        auto left = parse_unary(error);
        while (left && (is_operator("*") || is_operator("/") || is_operator("%"))) {
            const std::string op = current_.text;
            advance();
            auto right = parse_unary(error);
            if (!right) return nullptr;
            left = binary(op, std::move(left), std::move(right));
        }
        return left;
    }

    std::unique_ptr<Expr> parse_unary(std::string& error)
    {
        if (is_operator("+") || is_operator("-") || is_operator("!")) {
            auto expression = std::make_unique<Expr>();
            expression->kind = Expr::Kind::Unary;
            expression->text = current_.text;
            advance();
            auto operand = parse_unary(error);
            if (!operand) return nullptr;
            expression->args.push_back(std::move(operand));
            return expression;
        }
        return parse_primary(error);
    }

    std::unique_ptr<Expr> parse_primary(std::string& error)
    {
        if (current_.kind == TokenKind::Number) {
            auto expression = std::make_unique<Expr>();
            expression->kind = Expr::Kind::Number;
            expression->number = current_.number;
            advance();
            return expression;
        }
        if (current_.kind == TokenKind::String) {
            auto expression = std::make_unique<Expr>();
            expression->kind = Expr::Kind::String;
            expression->text = current_.text;
            advance();
            return expression;
        }
        if (current_.kind == TokenKind::Identifier) {
            const std::string name = current_.text;
            advance();
            if (current_.kind != TokenKind::LParen) {
                auto expression = std::make_unique<Expr>();
                expression->kind = Expr::Kind::Variable;
                expression->text = name;
                return expression;
            }
            advance();
            auto expression = std::make_unique<Expr>();
            expression->kind = Expr::Kind::Function;
            expression->text = name;
            if (current_.kind != TokenKind::RParen) {
                while (true) {
                    auto arg = parse_logical_or(error);
                    if (!arg) return nullptr;
                    expression->args.push_back(std::move(arg));
                    if (current_.kind != TokenKind::Comma)
                        break;
                    advance();
                }
            }
            if (current_.kind != TokenKind::RParen) {
                fail(error, "expected ')' after function arguments");
                return nullptr;
            }
            advance();
            return expression;
        }
        if (current_.kind == TokenKind::LParen) {
            advance();
            auto expression = parse_logical_or(error);
            if (!expression) return nullptr;
            if (current_.kind != TokenKind::RParen) {
                fail(error, "expected ')'");
                return nullptr;
            }
            advance();
            return expression;
        }
        fail(error, "expected a value");
        return nullptr;
    }

    static std::unique_ptr<Expr> binary(const std::string& op,
                                        std::unique_ptr<Expr> left,
                                        std::unique_ptr<Expr> right)
    {
        auto expression = std::make_unique<Expr>();
        expression->kind = Expr::Kind::Binary;
        expression->text = op;
        expression->args.push_back(std::move(left));
        expression->args.push_back(std::move(right));
        return expression;
    }

    bool is_operator(const char* op) const
    {
        return current_.kind == TokenKind::Operator && current_.text == op;
    }

    bool fail(std::string& error, const std::string& message) const
    {
        std::ostringstream stream;
        stream << message << " at offset " << current_.offset;
        error = stream.str();
        return false;
    }

    void advance() { current_ = lexer_.next(); }

    Lexer lexer_;
    Token current_;
};

bool require_number(const Value& value, double& number, std::string& error)
{
    if (value.kind != Value::Kind::Number) {
        error = "numeric value required";
        return false;
    }
    number = value.number;
    return true;
}

bool eval_expr(const Expr& expression, EvalContext& context, Value& out, std::string& error);

bool eval_function(const Expr& expression, EvalContext& context, Value& out, std::string& error)
{
    std::vector<Value> args;
    args.reserve(expression.args.size());
    for (const auto& arg : expression.args) {
        Value value;
        if (!eval_expr(*arg, context, value, error))
            return false;
        args.push_back(std::move(value));
    }

    const auto numeric_arg = [&](size_t index, double& value) -> bool {
        if (index >= args.size()) {
            error = expression.text + " missing argument";
            return false;
        }
        return require_number(args[index], value, error);
    };
    const auto arity = [&](size_t count) -> bool {
        if (args.size() == count) return true;
        error = expression.text + " expects " + std::to_string(count) + " argument(s)";
        return false;
    };

    if (expression.text == "ch" || expression.text == "chf" || expression.text == "chi") {
        if (!arity(1)) return false;
        if (args[0].kind != Value::Kind::String) {
            error = expression.text + " expects a string parameter name";
            return false;
        }
        const auto it = context.parameters.find(args[0].text);
        if (it == context.parameters.end()) {
            error = "unknown parameter '" + args[0].text + "'";
            return false;
        }
        out = Value::numeric(expression.text == "chi" ? std::round(it->second) : it->second);
        return true;
    }

    double a = 0.0;
    double b = 0.0;
    double c = 0.0;
    if (expression.text == "abs" && arity(1) && numeric_arg(0, a)) out = Value::numeric(std::abs(a));
    else if (expression.text == "sin" && arity(1) && numeric_arg(0, a)) out = Value::numeric(std::sin(a));
    else if (expression.text == "cos" && arity(1) && numeric_arg(0, a)) out = Value::numeric(std::cos(a));
    else if (expression.text == "tan" && arity(1) && numeric_arg(0, a)) out = Value::numeric(std::tan(a));
    else if (expression.text == "sinh" && arity(1) && numeric_arg(0, a)) out = Value::numeric(std::sinh(a));
    else if (expression.text == "cosh" && arity(1) && numeric_arg(0, a)) out = Value::numeric(std::cosh(a));
    else if (expression.text == "sqrt" && arity(1) && numeric_arg(0, a) && a >= 0.0) out = Value::numeric(std::sqrt(a));
    else if (expression.text == "exp" && arity(1) && numeric_arg(0, a)) out = Value::numeric(std::exp(a));
    else if (expression.text == "log" && arity(1) && numeric_arg(0, a) && a > 0.0) out = Value::numeric(std::log(a));
    else if (expression.text == "floor" && arity(1) && numeric_arg(0, a)) out = Value::numeric(std::floor(a));
    else if (expression.text == "ceil" && arity(1) && numeric_arg(0, a)) out = Value::numeric(std::ceil(a));
    else if (expression.text == "round" && arity(1) && numeric_arg(0, a)) out = Value::numeric(std::round(a));
    else if (expression.text == "min" && arity(2) && numeric_arg(0, a) && numeric_arg(1, b)) out = Value::numeric(std::min(a, b));
    else if (expression.text == "max" && arity(2) && numeric_arg(0, a) && numeric_arg(1, b)) out = Value::numeric(std::max(a, b));
    else if (expression.text == "pow" && arity(2) && numeric_arg(0, a) && numeric_arg(1, b)) out = Value::numeric(std::pow(a, b));
    else if (expression.text == "atan2" && arity(2) && numeric_arg(0, a) && numeric_arg(1, b))
        out = Value::numeric(std::atan2(a, b));
    else if (expression.text == "sign" && arity(1) && numeric_arg(0, a))
        out = Value::numeric(a > 0.0 ? 1.0 : (a < 0.0 ? -1.0 : 0.0));
    else if (expression.text == "clamp" && arity(3) && numeric_arg(0, a) && numeric_arg(1, b) && numeric_arg(2, c)) out = Value::numeric(std::clamp(a, b, c));
    else if (expression.text == "lerp" && arity(3) && numeric_arg(0, a) && numeric_arg(1, b) && numeric_arg(2, c)) out = Value::numeric(a + (b - a) * c);
    else {
        if (error.empty())
            error = "unknown function or invalid arguments: " + expression.text;
        return false;
    }

    if (!std::isfinite(out.number)) {
        error = "non-finite result from " + expression.text;
        return false;
    }
    return true;
}

bool eval_expr(const Expr& expression, EvalContext& context, Value& out, std::string& error)
{
    switch (expression.kind) {
    case Expr::Kind::Number:
        out = Value::numeric(expression.number);
        return true;
    case Expr::Kind::String:
        out = Value::string(expression.text);
        return true;
    case Expr::Kind::Variable: {
        if (expression.text == "PI" || expression.text == "M_PI") {
            out = Value::numeric(3.14159265358979323846);
            return true;
        }
        if (expression.text == "E" || expression.text == "M_E") {
            out = Value::numeric(2.71828182845904523536);
            return true;
        }
        double value = 0.0;
        if (!context.read_variable || !context.read_variable(expression.text, value)) {
            error = "unknown or non-numeric variable '" + expression.text + "'";
            return false;
        }
        out = Value::numeric(value);
        return true;
    }
    case Expr::Kind::Function:
        return eval_function(expression, context, out, error);
    case Expr::Kind::Unary: {
        Value operand;
        if (!eval_expr(*expression.args[0], context, operand, error)) return false;
        double value = 0.0;
        if (!require_number(operand, value, error)) return false;
        if (expression.text == "+") out = Value::numeric(value);
        else if (expression.text == "-") out = Value::numeric(-value);
        else out = Value::numeric(value == 0.0 ? 1.0 : 0.0);
        return true;
    }
    case Expr::Kind::Binary:
        break;
    }

    Value left;
    if (!eval_expr(*expression.args[0], context, left, error)) return false;
    double a = 0.0;
    if (!require_number(left, a, error)) return false;
    if (expression.text == "&&" && a == 0.0) {
        out = Value::numeric(0.0);
        return true;
    }
    if (expression.text == "||" && a != 0.0) {
        out = Value::numeric(1.0);
        return true;
    }

    Value right;
    if (!eval_expr(*expression.args[1], context, right, error)) return false;
    double b = 0.0;
    if (!require_number(right, b, error)) return false;

    if (expression.text == "+") out = Value::numeric(a + b);
    else if (expression.text == "-") out = Value::numeric(a - b);
    else if (expression.text == "*") out = Value::numeric(a * b);
    else if (expression.text == "/") {
        if (std::abs(b) <= std::numeric_limits<double>::epsilon()) {
            error = "division by zero";
            return false;
        }
        out = Value::numeric(a / b);
    } else if (expression.text == "%") {
        if (std::abs(b) <= std::numeric_limits<double>::epsilon()) {
            error = "modulo by zero";
            return false;
        }
        out = Value::numeric(std::fmod(a, b));
    } else if (expression.text == "<") out = Value::numeric(a < b ? 1.0 : 0.0);
    else if (expression.text == "<=") out = Value::numeric(a <= b ? 1.0 : 0.0);
    else if (expression.text == ">") out = Value::numeric(a > b ? 1.0 : 0.0);
    else if (expression.text == ">=") out = Value::numeric(a >= b ? 1.0 : 0.0);
    else if (expression.text == "==") out = Value::numeric(a == b ? 1.0 : 0.0);
    else if (expression.text == "!=") out = Value::numeric(a != b ? 1.0 : 0.0);
    else if (expression.text == "&&") out = Value::numeric(b != 0.0 ? 1.0 : 0.0);
    else if (expression.text == "||") out = Value::numeric(b != 0.0 ? 1.0 : 0.0);
    else {
        error = "unsupported operator '" + expression.text + "'";
        return false;
    }
    if (!std::isfinite(out.number)) {
        error = "expression produced a non-finite value";
        return false;
    }
    return true;
}

} // namespace

struct Program::Impl {
    std::vector<Statement> statements;
    std::unique_ptr<Expr> expression;
};

Program::Program() : impl_(std::make_shared<Impl>()) {}

bool Program::compile_statements(const std::string& source, Program& out, std::string& error)
{
    Program compiled;
    Parser parser(source);
    if (!parser.parse_statements(compiled.impl_->statements, error))
        return false;
    out = std::move(compiled);
    return true;
}

bool Program::compile_expression(const std::string& source, Program& out, std::string& error)
{
    Program compiled;
    Parser parser(source);
    compiled.impl_->expression = parser.parse_single_expression(error);
    if (!compiled.impl_->expression)
        return false;
    out = std::move(compiled);
    return true;
}

bool Program::execute(EvalContext& context, std::string& error) const
{
    for (const auto& statement : impl_->statements) {
        Value evaluated;
        if (!eval_expr(*statement.value, context, evaluated, error))
            return false;
        double value = 0.0;
        if (!require_number(evaluated, value, error))
            return false;
        if (statement.op != "=") {
            double current = 0.0;
            if (!context.read_variable || !context.read_variable(statement.target, current)) {
                error = "cannot read assignment target '" + statement.target + "'";
                return false;
            }
            if (statement.op == "+=") value = current + value;
            else if (statement.op == "-=") value = current - value;
            else if (statement.op == "*=") value = current * value;
            else if (statement.op == "/=") {
                if (std::abs(value) <= std::numeric_limits<double>::epsilon()) {
                    error = "division by zero in assignment";
                    return false;
                }
                value = current / value;
            } else if (statement.op == "%=") {
                if (std::abs(value) <= std::numeric_limits<double>::epsilon()) {
                    error = "modulo by zero in assignment";
                    return false;
                }
                value = std::fmod(current, value);
            }
        }
        if (!std::isfinite(value)) {
            error = "assignment produced a non-finite value";
            return false;
        }
        if (!context.write_variable || !context.write_variable(statement.target, value)) {
            error = "read-only or unsupported assignment target '" + statement.target + "'";
            return false;
        }
    }
    return true;
}

bool Program::evaluate(EvalContext& context, double& value, std::string& error) const
{
    if (!impl_->expression) {
        error = "program does not contain a selection expression";
        return false;
    }
    Value evaluated;
    if (!eval_expr(*impl_->expression, context, evaluated, error))
        return false;
    return require_number(evaluated, value, error);
}

bool Program::empty() const
{
    return impl_->statements.empty() && !impl_->expression;
}

} // namespace pcg::internal::elements::expression
