#include "IO/PdfFunction.h"
#include "IO/PdfDocument.h"
#include "IO/PdfLexer.h"
#include <algorithm>
#include <cmath>

namespace Pdf {

namespace {
QList<double> toDoubleList(const Document &document, const Object &object)
{
    QList<double> out;
    for (const Object &item : document.resolve(object).toArray())
        out.append(document.resolve(item).toReal());
    return out;
}
QList<int> toIntList(const Document &document, const Object &object)
{
    QList<int> out;
    for (const Object &item : document.resolve(object).toArray())
        out.append(int(document.resolve(item).toInt()));
    return out;
}

QList<PsNode> parsePsBlock(Lexer &lexer)
{
    QList<PsNode> nodes;
    while (true) {
        const Token token = lexer.next();
        if (token.kind == TokenKind::end)
            break;
        if (token.kind == TokenKind::keyword && token.bytes == "}")
            break;
        if (token.kind == TokenKind::keyword && token.bytes == "{") {
            PsNode node;
            node.kind = PsNode::block;
            node.children = parsePsBlock(lexer);
            nodes.append(node);
        } else if (token.kind == TokenKind::number) {
            PsNode node;
            node.kind = PsNode::number;
            node.value = token.number;
            nodes.append(node);
        } else if (token.kind == TokenKind::keyword) {
            PsNode node;
            node.kind = PsNode::op;
            node.name = token.bytes;
            nodes.append(node);
        }
    }
    return nodes;
}

void applyPsOperator(const QByteArray &op, QList<double> &stack)
{
    const auto pop = [&]() -> double {
        if (stack.isEmpty())
            return 0;
        const double value = stack.last();
        stack.removeLast();
        return value;
    };
    const auto push = [&](double value) { stack.append(value); };

    if (op == "add") {
        const double b = pop(), a = pop();
        push(a + b);
    } else if (op == "sub") {
        const double b = pop(), a = pop();
        push(a - b);
    } else if (op == "mul") {
        const double b = pop(), a = pop();
        push(a * b);
    } else if (op == "div") {
        const double b = pop(), a = pop();
        push(b != 0 ? a / b : 0);
    } else if (op == "idiv") {
        const long b = long(pop()), a = long(pop());
        push(b != 0 ? double(a / b) : 0);
    } else if (op == "mod") {
        const long b = long(pop()), a = long(pop());
        push(b != 0 ? double(a % b) : 0);
    } else if (op == "neg") {
        push(-pop());
    } else if (op == "abs") {
        push(std::abs(pop()));
    } else if (op == "sqrt") {
        const double a = pop();
        push(a > 0 ? std::sqrt(a) : 0);
    } else if (op == "sin") {
        push(std::sin(pop() * M_PI / 180.0));
    } else if (op == "cos") {
        push(std::cos(pop() * M_PI / 180.0));
    } else if (op == "atan") {
        const double den = pop(), num = pop();
        double degrees = std::atan2(num, den) * 180.0 / M_PI;
        if (degrees < 0)
            degrees += 360;
        push(degrees);
    } else if (op == "exp") {
        const double e = pop(), b = pop();
        push(std::pow(b, e));
    } else if (op == "ln") {
        const double a = pop();
        push(a > 0 ? std::log(a) : 0);
    } else if (op == "log") {
        const double a = pop();
        push(a > 0 ? std::log10(a) : 0);
    } else if (op == "ceiling") {
        push(std::ceil(pop()));
    } else if (op == "floor") {
        push(std::floor(pop()));
    } else if (op == "round") {
        push(std::round(pop()));
    } else if (op == "truncate") {
        push(std::trunc(pop()));
    } else if (op == "cvi") {
        push(double(long(pop())));
    } else if (op == "cvr") {
        // no-op: our stack is already real-valued
    } else if (op == "dup") {
        const double a = pop();
        push(a);
        push(a);
    } else if (op == "pop") {
        pop();
    } else if (op == "exch") {
        const double b = pop(), a = pop();
        push(b);
        push(a);
    } else if (op == "copy") {
        const int n = int(pop());
        if (n > 0 && n <= stack.size()) {
            const QList<double> tail = stack.mid(stack.size() - n);
            stack.append(tail);
        }
    } else if (op == "index") {
        const int n = int(pop());
        push(n >= 0 && n < stack.size() ? stack[stack.size() - 1 - n] : 0);
    } else if (op == "roll") {
        const int j = int(pop());
        const int n = int(pop());
        if (n > 0 && n <= stack.size()) {
            QList<double> part = stack.mid(stack.size() - n);
            const int shift = ((j % n) + n) % n;
            std::rotate(part.begin(), part.begin() + (n - shift) % n, part.end());
            for (int i = 0; i < n; ++i)
                stack[stack.size() - n + i] = part[i];
        }
    } else if (op == "eq") {
        const double b = pop(), a = pop();
        push(a == b ? 1 : 0);
    } else if (op == "ne") {
        const double b = pop(), a = pop();
        push(a != b ? 1 : 0);
    } else if (op == "gt") {
        const double b = pop(), a = pop();
        push(a > b ? 1 : 0);
    } else if (op == "ge") {
        const double b = pop(), a = pop();
        push(a >= b ? 1 : 0);
    } else if (op == "lt") {
        const double b = pop(), a = pop();
        push(a < b ? 1 : 0);
    } else if (op == "le") {
        const double b = pop(), a = pop();
        push(a <= b ? 1 : 0);
    } else if (op == "and") {
        const long b = long(pop()), a = long(pop());
        push(double(a & b));
    } else if (op == "or") {
        const long b = long(pop()), a = long(pop());
        push(double(a | b));
    } else if (op == "xor") {
        const long b = long(pop()), a = long(pop());
        push(double(a ^ b));
    } else if (op == "not") {
        push(pop() == 0 ? 1 : 0);
    } else if (op == "bitshift") {
        const long shift = long(pop()), a = long(pop());
        push(double(shift >= 0 ? (a << shift) : (a >> (-shift))));
    } else if (op == "true") {
        push(1);
    } else if (op == "false") {
        push(0);
    }
    // An unrecognised operator is a no-op; the calculator degrades gracefully.
}

void execPs(const QList<PsNode> &program, QList<double> &stack)
{
    for (qsizetype i = 0; i < program.size(); ++i) {
        const PsNode &node = program[i];
        if (node.kind == PsNode::number) {
            stack.append(node.value);
        } else if (node.kind == PsNode::op) {
            if (node.name == "if") {
                const bool condition = !stack.isEmpty() && stack.takeLast() != 0;
                if (i >= 1 && program[i - 1].kind == PsNode::block && condition)
                    execPs(program[i - 1].children, stack);
            } else if (node.name == "ifelse") {
                const bool condition = !stack.isEmpty() && stack.takeLast() != 0;
                if (i >= 2 && program[i - 2].kind == PsNode::block && program[i - 1].kind == PsNode::block)
                    execPs(condition ? program[i - 2].children : program[i - 1].children, stack);
            } else {
                applyPsOperator(node.name, stack);
            }
        }
        // block nodes are consumed by the if/ifelse that follows them
    }
}
}

double Function::sampleValue(const QList<int> &coords, int outputIndex) const
{
    qint64 index = 0, stride = 1;
    for (int i = 0; i < m_size.size(); ++i) {
        index += qint64(coords[i]) * stride;
        stride *= std::max(1, m_size[i]);
    }
    const qint64 bitOffset = (index * m_outputs + outputIndex) * m_bitsPerSample;
    quint64 value = 0;
    for (int b = 0; b < m_bitsPerSample; ++b) {
        const qint64 bitPos = bitOffset + b;
        const qint64 byteIndex = bitPos / 8;
        const int bitIndex = 7 - int(bitPos % 8);
        const int bit = byteIndex < m_samples.size() ? ((uchar(m_samples[byteIndex]) >> bitIndex) & 1) : 0;
        value = (value << 1) | quint64(bit);
    }
    return double(value);
}

QList<double> Function::evaluateSampled(const QList<double> &input) const
{
    const int m = m_size.size();
    if (m == 0 || m_outputs <= 0)
        return {};
    QList<int> lowCorner(m);
    QList<double> fraction(m);
    for (int i = 0; i < m; ++i) {
        const double domain0 = m_domain.value(i * 2, 0), domain1 = m_domain.value(i * 2 + 1, 1);
        const double encode0 = m_encode.value(i * 2, 0), encode1 = m_encode.value(i * 2 + 1, m_size[i] - 1);
        const double x = input.value(i, 0);
        const double t = domain1 > domain0 ? (x - domain0) / (domain1 - domain0) : 0;
        double position = encode0 + std::clamp(t, 0.0, 1.0) * (encode1 - encode0);
        position = std::clamp(position, 0.0, double(std::max(0, m_size[i] - 1)));
        int low = int(std::floor(position));
        low = std::min(low, std::max(0, m_size[i] - 2));
        lowCorner[i] = low;
        fraction[i] = m_size[i] > 1 ? position - low : 0;
    }

    QList<double> output(m_outputs, 0.0);
    const int corners = 1 << m;
    for (int mask = 0; mask < corners; ++mask) {
        double weight = 1;
        QList<int> coords(m);
        for (int i = 0; i < m; ++i) {
            const bool high = (mask >> i) & 1;
            coords[i] = std::min(lowCorner[i] + (high ? 1 : 0), std::max(0, m_size[i] - 1));
            weight *= high ? fraction[i] : (1 - fraction[i]);
        }
        if (weight == 0)
            continue;
        for (int o = 0; o < m_outputs; ++o)
            output[o] += weight * sampleValue(coords, o);
    }

    const double maxSample = double((quint64(1) << std::min(m_bitsPerSample, 32)) - 1);
    for (int o = 0; o < m_outputs; ++o) {
        const double decode0 = m_decode.value(o * 2, m_range.value(o * 2, 0));
        const double decode1 = m_decode.value(o * 2 + 1, m_range.value(o * 2 + 1, 1));
        output[o] = maxSample > 0 ? decode0 + (output[o] / maxSample) * (decode1 - decode0) : decode0;
    }
    return output;
}

QList<double> Function::evaluate(QList<double> input) const
{
    for (int i = 0; i * 2 + 1 < m_domain.size() && i < input.size(); ++i)
        input[i] = std::clamp(input[i], m_domain[i * 2], m_domain[i * 2 + 1]);

    QList<double> output;
    switch (m_type) {
    case 0:
        output = evaluateSampled(input);
        break;
    case 2: {
        const double x = input.value(0, 0);
        const int n = std::max(m_c0.size(), m_c1.size());
        for (int i = 0; i < n; ++i) {
            const double c0 = i < m_c0.size() ? m_c0[i] : 0;
            const double c1 = i < m_c1.size() ? m_c1[i] : 1;
            output.append(c0 + std::pow(x, m_exponent) * (c1 - c0));
        }
        break;
    }
    case 3: {
        const double x = input.value(0, 0);
        const int k = int(m_subFunctions.size());
        if (k == 0)
            break;
        const double lowest = m_domain.value(0, 0), highest = m_domain.value(1, 1);
        int segment = 0;
        while (segment < k - 1 && x >= m_bounds.value(segment, highest))
            ++segment;
        const double segmentLow = segment == 0 ? lowest : m_bounds.value(segment - 1, lowest);
        const double segmentHigh = segment == k - 1 ? highest : m_bounds.value(segment, highest);
        const double encode0 = m_stitchEncode.value(segment * 2, 0), encode1 = m_stitchEncode.value(segment * 2 + 1, 1);
        const double mapped =
            segmentHigh > segmentLow ? encode0 + (x - segmentLow) * (encode1 - encode0) / (segmentHigh - segmentLow) : encode0;
        output = m_subFunctions[segment].evaluate({mapped});
        break;
    }
    case 4: {
        QList<double> stack = input;
        execPs(m_program, stack);
        output = stack;
        break;
    }
    case 5: // an array of one-output functions, evaluated together
        for (const Function &sub : m_subFunctions)
            output.append(sub.evaluate(input).value(0, 0));
        break;
    default:
        break;
    }

    for (int i = 0; i * 2 + 1 < m_range.size() && i < output.size(); ++i)
        output[i] = std::clamp(output[i], m_range[i * 2], m_range[i * 2 + 1]);
    return output;
}

Function Function::load(const Document &document, const Object &functionObject)
{
    Function function;
    const Object resolved = document.resolve(functionObject);
    if (resolved.isArray()) {
        function.m_type = 5;
        for (const Object &item : resolved.toArray())
            function.m_subFunctions.push_back(load(document, item));
        return function;
    }
    if (!resolved.isDictionary())
        return function;

    const Dict &dict = resolved.toDict();
    function.m_type = int(document.resolve(dict.value(QStringLiteral("FunctionType"))).toInt(-1));
    function.m_domain = toDoubleList(document, dict.value(QStringLiteral("Domain")));
    function.m_range = toDoubleList(document, dict.value(QStringLiteral("Range")));

    switch (function.m_type) {
    case 0:
        function.m_size = toIntList(document, dict.value(QStringLiteral("Size")));
        function.m_bitsPerSample = int(document.resolve(dict.value(QStringLiteral("BitsPerSample"))).toInt(8));
        function.m_encode = toDoubleList(document, dict.value(QStringLiteral("Encode")));
        function.m_decode = toDoubleList(document, dict.value(QStringLiteral("Decode")));
        function.m_outputs = std::max(1, int(function.m_range.size() / 2));
        if (resolved.isStream())
            function.m_samples = document.streamData(resolved).bytes;
        break;
    case 2:
        function.m_c0 = toDoubleList(document, dict.value(QStringLiteral("C0")));
        function.m_c1 = toDoubleList(document, dict.value(QStringLiteral("C1")));
        if (function.m_c0.isEmpty())
            function.m_c0 = {0};
        if (function.m_c1.isEmpty())
            function.m_c1 = {1};
        function.m_exponent = document.resolve(dict.value(QStringLiteral("N"))).toReal(1);
        break;
    case 3:
        for (const Object &sub : document.resolve(dict.value(QStringLiteral("Functions"))).toArray())
            function.m_subFunctions.push_back(load(document, sub));
        function.m_bounds = toDoubleList(document, dict.value(QStringLiteral("Bounds")));
        function.m_stitchEncode = toDoubleList(document, dict.value(QStringLiteral("Encode")));
        break;
    case 4:
        if (resolved.isStream()) {
            const QByteArray code = document.streamData(resolved).bytes;
            Lexer lexer(code, 0);
            while (true) {
                const Token token = lexer.next();
                if (token.kind == TokenKind::end)
                    break;
                if (token.kind == TokenKind::keyword && token.bytes == "{") {
                    function.m_program = parsePsBlock(lexer);
                    break;
                }
            }
        }
        break;
    default:
        break;
    }
    return function;
}

}
