#ifndef ModelExtractionUtils_H
#define ModelExtractionUtils_H

#include <cmath>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <assert.h>
#include <stdexcept>

#include <boost/multiprecision/cpp_int.hpp>

using BigInt = boost::multiprecision::cpp_int;

// ============================================================
// Term hierarchy (ApplicationTerm and ConstantTerm)
// ============================================================
enum class TermType
{
    Application,
    Constant
};
enum class ConstType
{
    Integer,  // BigInt in C++
    Decimal,  // (unscaled BigInt + scale int) in C++
    Rational_ // Rational struct in C++
};

static std::string toStringBigInt(BigInt x) { return x.convert_to<std::string>(); };

static BigInt absBigInt(BigInt x) { return boost::multiprecision::abs(x); };


struct Rational
{
    BigInt num, den; // den always > 0 after reduce()

    static BigInt gcd_ll(BigInt a, BigInt b)
    {
        a = absBigInt(a);
        b = absBigInt(b);
        while (b)
        {
            a %= b;
            std::swap(a, b);
        }
        return a ? a : 1;
    }

    void reduce()
    {
        if (den < 0)
        {
            num = -num;
            den = -den;
        }
        BigInt g = gcd_ll(absBigInt(num), den);
        num /= g;
        den /= g;
    }

    Rational(BigInt n = 0, BigInt d = 1) : num(n), den(d) { reduce(); }

    static Rational ZERO() { return {0, 1}; }
    static Rational ONE() { return {1, 1}; }
    static Rational MONE() { return {-1, 1}; }

    Rational add(const Rational &o) const { return Rational(num * o.den + o.num * den, den * o.den); }
    Rational sub(const Rational &o) const { return Rational(num * o.den - o.num * den, den * o.den); }
    Rational mul(const Rational &o) const { return Rational(num * o.num, den * o.den); }
    Rational div(const Rational &o) const {
        BigInt a = num;
        BigInt b = den;
        BigInt c = o.num;
        BigInt d = o.den;

        BigInt g1 = gcd_ll(absBigInt(a), absBigInt(c));
        BigInt g2 = gcd_ll(b, d);

        a /= g1;
        c /= g1;
        b /= g2;
        d /= g2;

        return Rational(a * d, b * c);
    }

    Rational abs() const { return {absBigInt(num), den}; }

    // gcd(a/b, c/d) = gcd(a,c) / lcm(b,d)
    Rational gcd(const Rational &o) const
    {
        BigInt g_num = gcd_ll(absBigInt(num), absBigInt(o.num));
        BigInt lcm_den = den / gcd_ll(den, o.den) * o.den;
        return {g_num, lcm_den};
    }

    bool isZero() const { return num == 0; }
    bool isOne() const { return num == 1 && den == 1; }

    BigInt numerator() const { return num; }
    BigInt denominator() const { return den; }
    bool operator==(const Rational &o) const { return num == o.num && den == o.den; }

    std::string toString() const
    {
        if (den == 1)
            return toStringBigInt(num);
        else
            return toStringBigInt(num) + "/" + toStringBigInt(den);
    }

    std::string toSMTLibString() const
    {
        if (den == 1)
            return toStringBigInt(num);
        else
            return "(div " + toStringBigInt(num) + " " + toStringBigInt(den) + ")";
    }
};

struct Term
{
    TermType type;

    // --- ApplicationTerm ---
    std::string funcName;
    std::vector<std::shared_ptr<Term>> params;

    // --- ConstantTerm ---
    ConstType constType;
    BigInt intVal;      // Integer
    BigInt decUnscaled; // Decimal: unscaled value
    int decScale;          // Decimal: digits after decimal point
    Rational ratVal;       // Rational

    // Factories
    static std::shared_ptr<Term> makeApp(const std::string &fn,
                                        std::vector<std::shared_ptr<Term>> ps)
    {
        auto t = std::make_shared<Term>();
        t->type = TermType::Application;
        t->funcName = fn;
        t->params = std::move(ps);
        return t;
    }
    static std::shared_ptr<Term> makeInt(BigInt v)
    {
        auto t = std::make_shared<Term>();
        t->type = TermType::Constant;
        t->constType = ConstType::Integer;
        t->intVal = v;
        return t;
    }
    // Decimal: e.g. 0.75 → unscaled=75, scale=2
    static std::shared_ptr<Term> makeDecimal(BigInt unscaled, int scale)
    {
        auto t = std::make_shared<Term>();
        t->type = TermType::Constant;
        t->constType = ConstType::Decimal;
        t->decUnscaled = unscaled;
        t->decScale = scale;
        return t;
    }
    static std::shared_ptr<Term> makeRational(Rational r)
    {
        auto t = std::make_shared<Term>();
        t->type = TermType::Constant;
        t->constType = ConstType::Rational_;
        t->ratVal = r;
        return t;
    }

    static std::string toString(const std::shared_ptr<Term> &t)
    {
        if (t->type == TermType::Application)
        {
            std::string s = "(" + t->funcName;
            for (const auto &p : t->params)
                s += " " + toString(p);
            s += ")";
            return s;
        }
        else
        {
            switch (t->constType)
            {
            case ConstType::Integer:
                return toStringBigInt(t->intVal);
            case ConstType::Decimal:
                return toStringBigInt(t->decUnscaled) + " e-" + std::to_string(t->decScale);
            case ConstType::Rational_:
                return "rational: " + t->ratVal.toString();
            }
        }
        return "UnknownTerm";
    }
};

// ============================================================
// const2Rational
// ============================================================
inline Rational const2Rational(const std::shared_ptr<Term> &t)
{
    // --- ApplicationTerm: Z3/CVC5 return (/ 3 4), (- 5), etc. ---
    if (t->type == TermType::Application)
    {
        const auto &fn = t->funcName;
        const auto &p = t->params;
        if (fn == "+")
            return const2Rational(p[0]).add(const2Rational(p[1]));
        if (fn == "-")
        {
            if (p.size() == 1) // unary minus
                return const2Rational(p[0]).mul(Rational::MONE());
            else // binary minus
                return const2Rational(p[0]).sub(const2Rational(p[1]));
        }
        if (fn == "*")
            return const2Rational(p[0]).mul(const2Rational(p[1]));
        if (fn == "/") // Z3/CVC5 fraction node
            return const2Rational(p[0]).div(const2Rational(p[1]));
        throw std::runtime_error("Unknown function: " + fn);
    }

    // --- ConstantTerm ---
    if (t->type == TermType::Constant)
    {
        switch (t->constType)
        {

        case ConstType::Integer:
            // Integer → n/1
            return Rational(t->intVal, 1);

        case ConstType::Decimal:
        {
            // Decimal → unscaledValue / 10^scale
            BigInt unscaled = t->decUnscaled;
            BigInt scale = t->decScale;
            if (scale <= 0)
            {
                // e.g. scale=-1 means value = unscaled * 10
                BigInt factor = 1;
                for (int i = 0; i < -scale; ++i)
                    factor *= 10;
                return Rational(unscaled * factor, 1);
            }
            else
            {
                // e.g. 0.75 → 75 / 10^2 = 75/100 → reduces to 3/4
                BigInt denom = 1;
                for (int i = 0; i < scale; ++i)
                    denom *= 10;
                return Rational(unscaled, denom);
            }
        }

        case ConstType::Rational_:
            // Rational directly
            return t->ratVal;
        }
    }
    throw std::runtime_error("Unknown term structure");
}

// ============================================================
// getGcd
// ============================================================
inline Rational getGcd(const std::map<std::string, Rational> &assignment)
{
    Rational gcd = Rational::ONE();
    Rational old_gcd;
    for (const auto &[name, val] : assignment){
        if(val.isZero()) continue;
        old_gcd = gcd;
        gcd = gcd.gcd(val);
    }
    return gcd.abs(); // always positive
}



#endif
