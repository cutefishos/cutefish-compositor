// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shell/search/search_answer.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <string_view>
#include <sys/stat.h>
#include <vector>

#include "base/json.h"
#include "base/paths.h"
#include "base/process.h"
#include "base/strings.h"

namespace cutefish::shell {

namespace {

std::string lower(std::string text)
{
    for (char &c : text)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

std::string upper(std::string text)
{
    for (char &c : text)
        c = char(std::toupper(static_cast<unsigned char>(c)));
    return text;
}

std::string trimmedCopy(std::string_view text)
{
    return std::string(base::strings::trimmed(text));
}

std::string simplifiedCopy(std::string_view text)
{
    return base::strings::simplified(text);
}

constexpr const char *kRatesUrl = "https://api.frankfurter.app/latest?from=USD";
constexpr int64_t kRatesMaxAge = 24 * 60 * 60;

std::string formatNumber(double value)
{
    if (!std::isfinite(value))
        return {};
    std::ostringstream out;
    const double magnitude = std::fabs(value);
    if (magnitude != 0 && (magnitude >= 1e15 || magnitude < 1e-9))
        out.precision(6), out << std::scientific << value;
    else if (std::trunc(value) == value)
        out << std::fixed << std::setprecision(0) << value;
    else
        // Six digits, as Spotlight answers: "6.21371 miles", not the machine's
        // whole float.
        out.precision(6), out << value;
    std::string text = out.str();
    if (text.find('e') == std::string::npos) {
        const size_t dot = text.find('.');
        if (dot != std::string::npos) {
            while (!text.empty() && text.back() == '0')
                text.pop_back();
            if (!text.empty() && text.back() == '.')
                text.pop_back();
        }
    }
    return text.empty() ? "0" : text;
}

// A small recursive-descent calculator: + - * / % ^ and parentheses. Only the
// characters it understands get here, and nothing is evaluated as code.
class Calculator
{
public:
    explicit Calculator(std::string_view text)
        : m_text(text)
    {
    }

    std::optional<double> parse()
    {
        const double value = expression();
        skipSpace();
        if (!m_ok || m_pos != m_text.size())
            return std::nullopt;
        return value;
    }

private:
    void skipSpace()
    {
        while (m_pos < m_text.size() && m_text[m_pos] == ' ')
            ++m_pos;
    }

    bool accept(char c)
    {
        skipSpace();
        if (m_pos < m_text.size() && m_text[m_pos] == c) {
            ++m_pos;
            return true;
        }
        return false;
    }

    double expression()
    {
        double value = term();
        for (;;) {
            if (accept('+'))
                value += term();
            else if (accept('-'))
                value -= term();
            else
                return value;
        }
    }

    double term()
    {
        double value = power();
        for (;;) {
            if (accept('*'))
                value *= power();
            else if (accept('/')) {
                const double divisor = power();
                if (divisor == 0) {
                    m_ok = false;
                    return 0;
                }
                value /= divisor;
            } else if (accept('%')) {
                const double divisor = power();
                if (divisor == 0) {
                    m_ok = false;
                    return 0;
                }
                value = std::fmod(value, divisor);
            } else {
                return value;
            }
        }
    }

    double power()
    {
        double value = unary();
        if (accept('^'))
            value = std::pow(value, power());
        return value;
    }

    double unary()
    {
        if (accept('-'))
            return -unary();
        if (accept('+'))
            return unary();
        return primary();
    }

    double primary()
    {
        skipSpace();
        if (accept('(')) {
            const double value = expression();
            if (!accept(')'))
                m_ok = false;
            return value;
        }
        skipSpace();
        const size_t start = m_pos;
        bool dot = false;
        while (m_pos < m_text.size()) {
            const char c = m_text[m_pos];
            if (c >= '0' && c <= '9')
                ++m_pos;
            else if (c == '.' && !dot) {
                dot = true;
                ++m_pos;
            } else {
                break;
            }
        }
        if (m_pos == start) {
            m_ok = false;
            return 0;
        }
        return std::strtod(std::string(m_text.substr(start, m_pos - start)).c_str(), nullptr);
    }

    std::string_view m_text;
    size_t m_pos = 0;
    bool m_ok = true;
};

bool looksLikeCalculation(const std::string &text)
{
    bool digit = false;
    bool op = false;
    for (const char c : text) {
        if (c >= '0' && c <= '9')
            digit = true;
        else if (c == '.' || c == ' ')
            continue;
        else if (std::string_view("+-*/%^()").find(c) != std::string_view::npos)
            op = true;
        else
            return false;
    }
    return digit && op;
}

struct Unit
{
    const char *name;
    int category;
    // To the category's base unit: value * factor + offset.
    double factor;
    double offset;
};

constexpr int kLength = 1;
constexpr int kMass = 2;
constexpr int kVolume = 3;
constexpr int kTime = 4;
constexpr int kSpeed = 5;
constexpr int kData = 6;
constexpr int kArea = 7;
constexpr int kTemperature = 8;

const Unit kUnits[] = {
        // Length, metres.
        {"mm", kLength, 0.001, 0}, {"millimeter", kLength, 0.001, 0}, {"millimetre", kLength, 0.001, 0},
        {"cm", kLength, 0.01, 0}, {"centimeter", kLength, 0.01, 0}, {"centimetre", kLength, 0.01, 0},
        {"m", kLength, 1, 0}, {"meter", kLength, 1, 0}, {"metre", kLength, 1, 0},
        {"km", kLength, 1000, 0}, {"kilometer", kLength, 1000, 0}, {"kilometre", kLength, 1000, 0},
        {"in", kLength, 0.0254, 0}, {"inch", kLength, 0.0254, 0}, {"inches", kLength, 0.0254, 0},
        {"ft", kLength, 0.3048, 0}, {"foot", kLength, 0.3048, 0}, {"feet", kLength, 0.3048, 0},
        {"yd", kLength, 0.9144, 0}, {"yard", kLength, 0.9144, 0}, {"yards", kLength, 0.9144, 0},
        {"mi", kLength, 1609.344, 0}, {"mile", kLength, 1609.344, 0}, {"miles", kLength, 1609.344, 0},
        {"nmi", kLength, 1852, 0},
        // Mass, grams.
        {"mg", kMass, 0.001, 0}, {"g", kMass, 1, 0}, {"gram", kMass, 1, 0}, {"grams", kMass, 1, 0},
        {"kg", kMass, 1000, 0}, {"kilogram", kMass, 1000, 0}, {"kilograms", kMass, 1000, 0},
        {"t", kMass, 1000000, 0}, {"tonne", kMass, 1000000, 0},
        {"oz", kMass, 28.349523125, 0}, {"ounce", kMass, 28.349523125, 0}, {"ounces", kMass, 28.349523125, 0},
        {"lb", kMass, 453.59237, 0}, {"lbs", kMass, 453.59237, 0}, {"pound", kMass, 453.59237, 0},
        {"pounds", kMass, 453.59237, 0}, {"st", kMass, 6350.29318, 0}, {"stone", kMass, 6350.29318, 0},
        // Volume, litres.
        {"ml", kVolume, 0.001, 0}, {"l", kVolume, 1, 0}, {"liter", kVolume, 1, 0}, {"litre", kVolume, 1, 0},
        {"gal", kVolume, 3.785411784, 0}, {"gallon", kVolume, 3.785411784, 0},
        {"qt", kVolume, 0.946352946, 0}, {"quart", kVolume, 0.946352946, 0},
        {"pt", kVolume, 0.473176473, 0}, {"pint", kVolume, 0.473176473, 0},
        {"cup", kVolume, 0.2365882365, 0},
        {"floz", kVolume, 0.0295735295625, 0},
        {"tbsp", kVolume, 0.01478676478125, 0}, {"tsp", kVolume, 0.00492892159375, 0},
        // Time, seconds.
        {"ms", kTime, 0.001, 0}, {"s", kTime, 1, 0}, {"sec", kTime, 1, 0}, {"second", kTime, 1, 0},
        {"seconds", kTime, 1, 0}, {"min", kTime, 60, 0}, {"minute", kTime, 60, 0}, {"minutes", kTime, 60, 0},
        {"h", kTime, 3600, 0}, {"hr", kTime, 3600, 0}, {"hour", kTime, 3600, 0}, {"hours", kTime, 3600, 0},
        {"day", kTime, 86400, 0}, {"days", kTime, 86400, 0},
        {"week", kTime, 604800, 0}, {"weeks", kTime, 604800, 0},
        // Speed, metres a second.
        {"kmh", kSpeed, 1 / 3.6, 0}, {"kph", kSpeed, 1 / 3.6, 0}, {"mph", kSpeed, 0.44704, 0},
        {"m/s", kSpeed, 1, 0}, {"knot", kSpeed, 0.514444, 0}, {"knots", kSpeed, 0.514444, 0},
        // Data, bytes; decimal, as disks are sold.
        {"b", kData, 1, 0}, {"byte", kData, 1, 0}, {"bytes", kData, 1, 0},
        {"kb", kData, 1000, 0}, {"mb", kData, 1e6, 0}, {"gb", kData, 1e9, 0}, {"tb", kData, 1e12, 0},
        {"kib", kData, 1024, 0}, {"mib", kData, 1048576, 0}, {"gib", kData, 1073741824, 0},
        {"tib", kData, 1099511627776, 0},
        // Area, square metres.
        {"m2", kArea, 1, 0}, {"km2", kArea, 1e6, 0}, {"cm2", kArea, 0.0001, 0},
        {"ft2", kArea, 0.09290304, 0}, {"yd2", kArea, 0.83612736, 0},
        {"acre", kArea, 4046.8564224, 0}, {"acres", kArea, 4046.8564224, 0},
        {"ha", kArea, 10000, 0}, {"hectare", kArea, 10000, 0},
        // Temperature, kelvin.
        {"c", kTemperature, 1, 273.15}, {"celsius", kTemperature, 1, 273.15},
        {"f", kTemperature, 5.0 / 9.0, 255.3722222222222},
        {"fahrenheit", kTemperature, 5.0 / 9.0, 255.3722222222222},
        {"k", kTemperature, 1, 0}, {"kelvin", kTemperature, 1, 0},
};

const Unit *findUnit(const std::string &name)
{
    const std::string lowerName = lower(name);
    for (const Unit &unit : kUnits) {
        if (lowerName == unit.name)
            return &unit;
    }
    return nullptr;
}

std::string ratesPath()
{
    return base::paths::cacheHome() + "/cutefish/search-rates.json";
}

// USD to each currency, from the cache; empty when it holds nothing usable.
std::map<std::string, double> currencyRates()
{
    std::map<std::string, double> rates;
    struct stat info;
    if (stat(ratesPath().c_str(), &info) != 0)
        return rates;
    std::ifstream file(ratesPath());
    if (!file)
        return rates;
    const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    const base::json::Value document = base::json::Value::parse(text).value_or(base::json::Value());
    for (const auto &[code, rate] : document["rates"].toObject()) {
        if (rate.isNumber())
            rates[code] = rate.toDouble();
    }
    rates["USD"] = 1;
    return rates;
}

bool ratesAreStale()
{
    struct stat info;
    if (stat(ratesPath().c_str(), &info) != 0)
        return true;
    return std::time(nullptr) - info.st_mtime > kRatesMaxAge;
}

std::string formatRate(double value)
{
    return formatNumber(value);
}

} // namespace

void refreshCurrencyRatesIfStale()
{
    if (!ratesAreStale())
        return;
    // Written in place: a half-written file parses as nothing and is retried.
    base::Process::startDetached("curl", {"-fsS", "--max-time", "15", "-o", ratesPath(), kRatesUrl});
}

std::optional<SearchAnswer> answerFor(const std::string &query)
{
    const std::string text = simplifiedCopy(query);
    if (text.empty())
        return std::nullopt;

    if (looksLikeCalculation(text)) {
        Calculator calculator(text);
        if (const auto value = calculator.parse()) {
            return SearchAnswer {.value = formatNumber(*value), .expression = text};
        }
    }

    // "<number> <unit> to <unit>", with "in" and "as" as the same word.
    std::string separator;
    size_t at = std::string::npos;
    for (const char *candidate : {" to ", " in ", " as "}) {
        const size_t found = text.find(candidate);
        if (found != std::string::npos && (at == std::string::npos || found < at)) {
            at = found;
            separator = candidate;
        }
    }
    if (at == std::string::npos)
        return std::nullopt;

    std::string left = trimmedCopy(text.substr(0, at));
    std::string right = trimmedCopy(text.substr(at + separator.size()));
    if (right.find(' ') != std::string::npos)
        return std::nullopt;

    // The number, then the unit it counts.
    size_t split = 0;
    bool dot = false;
    while (split < left.size()) {
        const char c = left[split];
        if ((c >= '0' && c <= '9') || c == ',')
            ++split;
        else if (c == '.' && !dot)
            dot = true, ++split;
        else if (std::isspace(static_cast<unsigned char>(c)))
            ++split;
        else
            break;
    }
    std::string numberText = trimmedCopy(left.substr(0, split));
    const std::string unitText = trimmedCopy(left.substr(split));
    if (numberText.empty() || unitText.empty())
        return std::nullopt;
    numberText.erase(std::remove(numberText.begin(), numberText.end(), ','), numberText.end());
    char *end = nullptr;
    const double amount = std::strtod(numberText.c_str(), &end);
    if (!end || *end != '\0')
        return std::nullopt;

    const std::string from = lower(unitText);
    const std::string to = lower(right);

    // Currencies first: they are not units of a fixed size.
    std::map<std::string, double> rates = currencyRates();
    if (rates.contains(upper(from)) || rates.contains(upper(to))) {
        refreshCurrencyRatesIfStale();
        const auto fromRate = rates.find(upper(from));
        const auto toRate = rates.find(upper(to));
        if (fromRate == rates.end() || toRate == rates.end() || fromRate->second <= 0)
            return std::nullopt;
        const double result = amount / fromRate->second * toRate->second;
        return SearchAnswer {.value = formatRate(result) + " " + upper(to),
                             .expression = text};
    }

    const Unit *source = findUnit(from);
    const Unit *target = findUnit(to);
    if (!source || !target || source->category != target->category)
        return std::nullopt;
    const double base = amount * source->factor + source->offset;
    const double result = (base - target->offset) / target->factor;
    return SearchAnswer {.value = formatNumber(result) + " " + to, .expression = text};
}

} // namespace cutefish::shell
