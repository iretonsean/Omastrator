#pragma once
#include <QByteArray>
#include <QList>

// A minimal hand-written PDF writer for test fixtures (PDF.md asks for one
// "inside the tests", alongside PDFs from Omastrator's own export). Objects
// are numbered in add() order, 1-based; a classic xref table is always used,
// since the xref-stream path gets its own fixture built by hand instead.
class PdfFixtureBuilder {
public:
    // Returns the new object's number.
    int add(const QByteArray &body) { m_objects.append(body); return m_objects.size(); }
    int addDict(const QByteArray &dictAttributes) { return add("<< " + dictAttributes + " >>"); }
    int addStream(const QByteArray &dictAttributes, const QByteArray &streamBytes)
    {
        return add("<< " + dictAttributes + " /Length " + QByteArray::number(streamBytes.size()) + " >>\nstream\n" + streamBytes
                    + "\nendstream");
    }
    // The next object's number, for a forward reference before it's added.
    int nextNumber() const { return int(m_objects.size()) + 1; }

    QByteArray build(int rootObjectNumber, const QByteArray &trailerExtra = {}) const
    {
        QByteArray out = "%PDF-1.5\n";
        QList<int> offsets{0};
        for (int i = 0; i < m_objects.size(); ++i) {
            offsets.append(int(out.size()));
            out += QByteArray::number(i + 1) + " 0 obj\n" + m_objects[i] + "\nendobj\n";
        }
        const int xrefOffset = int(out.size());
        const int count = int(m_objects.size()) + 1;
        out += "xref\n0 " + QByteArray::number(count) + "\n0000000000 65535 f \n";
        for (int i = 1; i < offsets.size(); ++i)
            out += QByteArray::number(offsets[i]).rightJustified(10, '0') + " 00000 n \n";
        out += "trailer\n<< /Size " + QByteArray::number(count) + " /Root " + QByteArray::number(rootObjectNumber) + " 0 R";
        if (!trailerExtra.isEmpty())
            out += " " + trailerExtra;
        out += " >>\nstartxref\n" + QByteArray::number(xrefOffset) + "\n%%EOF";
        return out;
    }

private:
    QList<QByteArray> m_objects;
};
