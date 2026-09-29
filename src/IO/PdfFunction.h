#pragma once
#include "IO/PdfObject.h"
#include <QList>
#include <vector>

// PDF function dictionaries: type 0 (sampled), 2 (exponential), 3
// (stitching) and 4 (a small PostScript calculator), used for shading color
// ramps and Separation/DeviceN tint transforms. Types 4-7 shadings (mesh)
// have no function evaluation need and are handled, or left out, elsewhere.
namespace Pdf {
class Document;

struct PsNode {
    enum Kind { number, op, block } kind = number;
    double value = 0;
    QByteArray name;
    QList<PsNode> children; // the proc body, when kind == block
};

class Function {
public:
    static Function load(const Document &document, const Object &functionObject);
    bool isValid() const { return m_type >= 0; }
    QList<double> evaluate(QList<double> input) const;

private:
    int m_type = -1;
    QList<double> m_domain;
    QList<double> m_range;

    // Type 0: sampled.
    QList<int> m_size;
    int m_bitsPerSample = 8;
    int m_outputs = 1;
    QList<double> m_encode;
    QList<double> m_decode;
    QByteArray m_samples;

    // Type 2: exponential interpolation.
    QList<double> m_c0;
    QList<double> m_c1;
    double m_exponent = 1;

    // Type 3: stitching.
    std::vector<Function> m_subFunctions; // also holds an array-of-functions (m_type == 5, one output each)
    QList<double> m_bounds;
    QList<double> m_stitchEncode;

    // Type 4: a PostScript calculator program.
    QList<PsNode> m_program;

    static Function loadAt(const Document &document, const Object &functionObject, int depth, int &budget);
    QList<double> evaluateSampled(const QList<double> &input) const;
    double sampleValue(const QList<int> &coords, int outputIndex) const;
};

}
