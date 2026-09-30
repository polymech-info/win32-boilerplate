#ifndef APP_TYPES_H
#define APP_TYPES_H

# include <string>
# include <memory>
# include <map>
# include <vector>
#include <string>

struct ArgValue
{
    enum class Type { Empty, Bool, Int, Double, String };

    Type Type = Type::Empty;

    bool        Bool = false;
    long long   Int = 0;
    double      Double = 0.0;
    std::string String;

    ArgValue() = default;
    ArgValue(bool value) : Type(Type::Bool), Bool(value) {}
    ArgValue(int value) : Type(Type::Int), Int(value) {}
    ArgValue(long long value) : Type(Type::Int), Int(value) {}
    ArgValue(float value) : Type(Type::Double), Double(value) {}
    ArgValue(double value) : Type(Type::Double), Double(value) {}
    ArgValue(const char* value) : Type(Type::String), String(value) {}
    ArgValue(std::string value) : Type(Type::String), String(std::move(value)) {}
};

using ArgsMap = std::map<std::string, ArgValue>;

#endif