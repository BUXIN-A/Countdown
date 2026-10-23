// =============================================================================
// json.h - 极简 JSON 值类
//
// 仅支持配置场景所需子集：object / array / string / number(double) / bool / null。
// 提供 parse() 与 serialize() 两个静态方法。字符串序列化时按 JSON 规范转义。
// 不依赖 STL 之外的任何库，全部头文件内联实现。
// =============================================================================
#pragma once
#include <string>
#include <vector>
#include <map>
#include <variant>
#include <memory>
#include <sstream>
#include <iomanip>
#include <cstdint>
#include <stdexcept>
#include <cstdlib>
#include <cmath>
#include <cwctype>

namespace mini_json {

class Value;
using Object = std::vector<std::pair<std::string, Value>>;
using Array  = std::vector<Value>;

class Value {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Value() : type_(Type::Null) {}
    Value(bool b) : type_(Type::Bool), bool_(b) {}
    Value(int v) : type_(Type::Number), num_(static_cast<double>(v)) {}
    Value(double v) : type_(Type::Number), num_(v) {}
    Value(const char* s) : type_(Type::String), str_(s ? s : "") {}
    Value(std::string s) : type_(Type::String), str_(std::move(s)) {}
    Value(Array a) : type_(Type::Array), arr_(std::move(a)) {}
    Value(Object o) : type_(Type::Object), obj_(std::move(o)) {}

    Type type() const { return type_; }
    bool is_null() const { return type_ == Type::Null; }
    bool is_object() const { return type_ == Type::Object; }

    // 对象访问
    const Value* find(const std::string& key) const {
        if (type_ != Type::Object) return nullptr;
        for (const auto& kv : obj_) if (kv.first == key) return &kv.second;
        return nullptr;
    }
    const std::string& as_string(const std::string& fallback = "") const {
        return (type_ == Type::String) ? str_ : fallback;
    }
    double as_number(double fallback = 0.0) const {
        return (type_ == Type::Number) ? num_ : fallback;
    }
    bool as_bool(bool fallback = false) const {
        return (type_ == Type::Bool) ? bool_ : fallback;
    }

    // ---- 序列化 ----
    static std::string serialize(const Value& v, bool pretty = false, int indent = 0) {
        std::ostringstream os;
        write(os, v, pretty, indent);
        return os.str();
    }

    // ---- 解析 ----
    static Value parse(const std::string& text) {
        Parser p(text);
        p.skip_ws();
        Value v = p.parse_value();
        p.skip_ws();
        if (!p.at_end()) throw std::runtime_error("mini_json: trailing characters");
        return v;
    }

private:
    Type type_;
    bool bool_ = false;
    double num_ = 0.0;
    std::string str_;
    Array arr_;
    Object obj_;

    // ---------- 序列化实现 ----------
    static void write(std::ostringstream& os, const Value& v, bool pretty, int indent) {
        switch (v.type_) {
            case Type::Null:   os << "null"; break;
            case Type::Bool:   os << (v.bool_ ? "true" : "false"); break;
            case Type::Number: write_number(os, v.num_); break;
            case Type::String: write_string(os, v.str_); break;
            case Type::Array: {
                os << "[";
                if (v.arr_.empty()) { os << "]"; break; }
                for (size_t i = 0; i < v.arr_.size(); ++i) {
                    if (pretty) newline(os, indent + 1);
                    write(os, v.arr_[i], pretty, indent + 1);
                    if (i + 1 < v.arr_.size()) os << ",";
                }
                if (pretty) newline(os, indent);
                os << "]";
                break;
            }
            case Type::Object: {
                os << "{";
                if (v.obj_.empty()) { os << "}"; break; }
                for (size_t i = 0; i < v.obj_.size(); ++i) {
                    if (pretty) newline(os, indent + 1);
                    write_string(os, v.obj_[i].first);
                    os << (pretty ? ": " : ":");
                    write(os, v.obj_[i].second, pretty, indent + 1);
                    if (i + 1 < v.obj_.size()) os << ",";
                }
                if (pretty) newline(os, indent);
                os << "}";
                break;
            }
        }
    }

    static void newline(std::ostringstream& os, int indent) {
        os << "\n";
        for (int i = 0; i < indent; ++i) os << "    ";
    }

    static void write_number(std::ostringstream& os, double v) {
        // 整数则不带小数点
        double whole;
        if (std::modf(v, &whole) == 0.0 && std::abs(v) < 1e15) {
            os << static_cast<long long>(v);
        } else {
            std::ostringstream tmp;
            tmp << std::setprecision(15) << v;
            os << tmp.str();
        }
    }

    static void write_string(std::ostringstream& os, const std::string& s) {
        os << "\"";
        for (unsigned char c : s) {
            switch (c) {
                case '\"': os << "\\\""; break;
                case '\\': os << "\\\\"; break;
                case '\b': os << "\\b"; break;
                case '\f': os << "\\f"; break;
                case '\n': os << "\\n"; break;
                case '\r': os << "\\r"; break;
                case '\t': os << "\\t"; break;
                default:
                    if (c < 0x20) {
                        // 控制字符用 \uXXXX
                        os << "\\u" << std::hex << std::setw(4) << std::setfill('0') << (int)c
                           << std::dec << std::setw(0);
                    } else {
                        os << static_cast<char>(c);
                    }
            }
        }
        os << "\"";
    }

    // ---------- 解析实现 ----------
    struct Parser {
        // 最大嵌套深度：防止恶意/异常的超深嵌套 JSON 递归耗尽栈
        static const int kMaxDepth = 128;
        const std::string& s;
        size_t i = 0;
        int depth = 0;
        Parser(const std::string& t) : s(t) {}

        bool at_end() const { return i >= s.size(); }
        char peek() const { return at_end() ? '\0' : s[i]; }
        char next() { return at_end() ? '\0' : s[i++]; }

        void skip_ws() {
            while (!at_end()) {
                char c = s[i];
                if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { ++i; }
                else break;
            }
        }

        Value parse_value() {
            skip_ws();
            if (at_end()) throw std::runtime_error("mini_json: unexpected end");
            char c = peek();
            if (c == '{') return parse_object();
            if (c == '[') return parse_array();
            if (c == '"') return Value(parse_string());
            if (c == 't' || c == 'f') return parse_bool();
            if (c == 'n') return parse_null();
            return parse_number();
        }

        Value parse_object() {
            if (++depth > kMaxDepth) throw std::runtime_error("mini_json: nesting too deep");
            Object o;
            ++i; // consume '{'
            skip_ws();
            if (peek() == '}') { ++i; --depth; return Value(std::move(o)); }
            while (true) {
                skip_ws();
                if (peek() != '"') throw std::runtime_error("mini_json: expected string key");
                std::string key = parse_string();
                skip_ws();
                if (next() != ':') throw std::runtime_error("mini_json: expected ':'");
                Value val = parse_value();
                o.emplace_back(std::move(key), std::move(val));
                skip_ws();
                char c = next();
                if (c == ',') continue;
                if (c == '}') break;
                throw std::runtime_error("mini_json: expected ',' or '}'");
            }
            --depth;
            return Value(std::move(o));
        }

        Value parse_array() {
            if (++depth > kMaxDepth) throw std::runtime_error("mini_json: nesting too deep");
            Array a;
            ++i; // consume '['
            skip_ws();
            if (peek() == ']') { ++i; --depth; return Value(std::move(a)); }
            while (true) {
                Value v = parse_value();
                a.emplace_back(std::move(v));
                skip_ws();
                char c = next();
                if (c == ',') continue;
                if (c == ']') break;
                throw std::runtime_error("mini_json: expected ',' or ']'");
            }
            --depth;
            return Value(std::move(a));
        }

        std::string parse_string() {
            std::string out;
            if (next() != '"') throw std::runtime_error("mini_json: expected '\"'");
            while (!at_end()) {
                char c = next();
                if (c == '"') return out;
                if (c == '\\') {
                    char e = next();
                    switch (e) {
                        case '"': out.push_back('"'); break;
                        case '\\': out.push_back('\\'); break;
                        case '/': out.push_back('/'); break;
                        case 'b': out.push_back('\b'); break;
                        case 'f': out.push_back('\f'); break;
                        case 'n': out.push_back('\n'); break;
                        case 'r': out.push_back('\r'); break;
                        case 't': out.push_back('\t'); break;
                        case 'u': {
                            // \uXXXX -> UTF-8；代理对必须成对出现，否则视为非法 JSON
                            unsigned cp = parse_hex4();
                            if (cp >= 0xD800 && cp <= 0xDBFF) {
                                // 高代理：必须紧跟 \u 低代理
                                if (peek() == '\\' && i + 1 < s.size() && s[i + 1] == 'u') {
                                    ++i; ++i; // 消费 '\\' 与 'u'
                                    unsigned lo = parse_hex4();
                                    if (lo < 0xDC00 || lo > 0xDFFF)
                                        throw std::runtime_error("mini_json: bad surrogate pair");
                                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                                } else {
                                    throw std::runtime_error("mini_json: unpaired surrogate");
                                }
                            } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                                // 孤立的低代理
                                throw std::runtime_error("mini_json: stray low surrogate");
                            }
                            encode_utf8(cp, out);
                            break;
                        }
                        default: throw std::runtime_error("mini_json: bad escape");
                    }
                } else {
                    out.push_back(c);
                }
            }
            throw std::runtime_error("mini_json: unterminated string");
        }

        unsigned parse_hex4() {
            unsigned v = 0;
            for (int k = 0; k < 4; ++k) {
                char c = next();
                v <<= 4;
                if (c >= '0' && c <= '9') v |= (c - '0');
                else if (c >= 'a' && c <= 'f') v |= (c - 'a' + 10);
                else if (c >= 'A' && c <= 'F') v |= (c - 'A' + 10);
                else throw std::runtime_error("mini_json: bad \\u hex");
            }
            return v;
        }

        static void encode_utf8(unsigned cp, std::string& out) {
            if (cp < 0x80) {
                out.push_back(static_cast<char>(cp));
            } else if (cp < 0x800) {
                out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            } else if (cp < 0x10000) {
                out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            } else {
                out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            }
        }

        Value parse_bool() {
            if (s.compare(i, 4, "true") == 0) { i += 4; return Value(true); }
            if (s.compare(i, 5, "false") == 0) { i += 5; return Value(false); }
            throw std::runtime_error("mini_json: invalid literal");
        }

        Value parse_null() {
            if (s.compare(i, 4, "null") == 0) { i += 4; return Value(); }
            throw std::runtime_error("mini_json: invalid literal");
        }

        Value parse_number() {
            size_t start = i;
            if (peek() == '-') ++i;
            while (!at_end()) {
                char c = s[i];
                if ((c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-') {
                    ++i;
                } else break;
            }
            std::string num = s.substr(start, i - start);
            try {
                return Value(std::stod(num));
            } catch (...) {
                throw std::runtime_error("mini_json: bad number");
            }
        }
    };
};

} // namespace mini_json
