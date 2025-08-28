#include <iostream>
#include <string>
#include <cctype>
#include <cmath>
#include <stdexcept>
using namespace std;

class Parser {
    string expr;
    size_t pos = 0;

public:
    explicit Parser(string s) : expr(std::move(s)) {}
    double parse() {
        double v = parseExpression();
        skipSpaces();
        if (pos != expr.size()) {
            throw runtime_error("Unexpected trailing input at pos " + to_string(pos));
        }
        return v;
    }

private:
    // ---- utilities ----
    void skipSpaces() {
        while (pos < expr.size() && isspace(static_cast<unsigned char>(expr[pos]))) pos++;
    }
    bool match(char c) {
        skipSpaces();
        if (pos < expr.size() && expr[pos] == c) { ++pos; return true; }
        return false;
    }
    bool matchStr(const char* s) {
        skipSpaces();
        size_t i = 0;
        size_t start = pos;
        while (s[i] && start + i < expr.size() && expr[start + i] == s[i]) i++;
        if (!s[i]) { pos = start + i; return true; }
        return false;
    }
    char peek() {
        skipSpaces();
        return (pos < expr.size() ? expr[pos] : '\0');
    }
    bool nextStartsFactor() {
        char c = peek();
        // Start of a factor is: '(' or digit or '.'
        // (We intentionally do NOT treat '+' or '-' as implicit multiply starters.)
        return (c == '(') || isdigit(static_cast<unsigned char>(c)) || (c == '.');
    }

    // ---- grammar ----
    // expression := term (('+'|'-') term)*
    double parseExpression() {
        double val = parseTerm();
        while (true) {
            if (match('+'))      val += parseTerm();
            else if (match('-')) val -= parseTerm();
            else break;
        }
        return val;
    }

    // term := power ( ( '*' | '/' | '%' | implicitMul ) power )*
    // implicitMul occurs when another factor begins without an explicit operator
    double parseTerm() {
        double val = parsePower();
        while (true) {
            if (match('*')) {
                val *= parsePower();
            } else if (match('/')) {
                double d = parsePower();
                if (d == 0) throw runtime_error("Division by zero");
                val /= d;
            } else if (match('%')) {
                double rhs = parsePower();
                long long a = static_cast<long long>(val);
                long long b = static_cast<long long>(rhs);
                if (b == 0) throw runtime_error("Modulo by zero");
                val = static_cast<double>(a % b);
            } else if (nextStartsFactor()) {
                // implicit multiplication: e.g., 2(3+4) or (1+2)(3+4) or 3.5(2)
                val *= parsePower();
            } else {
                break;
            }
        }
        return val;
    }

    // power := factor ( ('^' | '**') power )?
    // Right-associative: a^b^c == a^(b^c)
    double parsePower() {
        double base = parseFactor();
        if (matchStr("**") || match('^')) {
            double exp = parsePower(); // recurse for right-assoc
            base = pow(base, exp);
        }
        return base;
    }

    // factor := number | '(' expression ')' | unary ('+'|'-') factor
    double parseFactor() {
        skipSpaces();
        if (match('+')) return parseFactor();      // unary plus
        if (match('-')) return -parseFactor();     // unary minus
        if (match('(')) {
            double v = parseExpression();
            if (!match(')')) throw runtime_error("Missing ')'");
            return v;
        }
        return parseNumber();
    }

    double parseNumber() {
        skipSpaces();
        size_t start = pos;
        bool seenDigit = false, seenDot = false;

        // integer/float (with optional scientific notation)
        while (pos < expr.size()) {
            char c = expr[pos];
            if (isdigit(static_cast<unsigned char>(c))) { seenDigit = true; ++pos; }
            else if (c == '.' && !seenDot) { seenDot = true; ++pos; }
            else break;
        }
        // scientific notation like 1e-3
        if (pos < expr.size() && (expr[pos] == 'e' || expr[pos] == 'E')) {
            size_t save = pos++;
            if (pos < expr.size() && (expr[pos] == '+' || expr[pos] == '-')) ++pos;
            bool expDigits = false;
            while (pos < expr.size() && isdigit(static_cast<unsigned char>(expr[pos]))) {
                expDigits = true; ++pos;
            }
            if (!expDigits) pos = save; // roll back if not a valid exponent
        }

        if (!seenDigit && !(pos > start)) {
            throw runtime_error("Expected number at pos " + to_string(pos));
        }
        return stod(expr.substr(start, pos - start));
    }
};

int main() {
    cout << "=============================\n";
    cout << "   C++ Calculator (PEMDAS)\n";
    cout << "=============================\n\n";

    string line;
    while (true) {
        cout << "Enter expression (or Q to quit): ";
        if (!getline(cin, line)) break;
        if (line == "q" || line == "Q") break;

        try {
            Parser p(line);
            double result = p.parse();
            cout << "Result: " << result << "\n\n";
        } catch (const exception& e) {
            cout << "Error: " << e.what() << "\n\n";
        }
    }
    cout << "Goodbye!\n";
    return 0;
}
