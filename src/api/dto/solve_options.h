#ifndef GTO_API_SOLVE_OPTIONS_H
#define GTO_API_SOLVE_OPTIONS_H

#include <QJsonObject>
#include <QString>

namespace GtoApi {

class SolveOptions
{
public:
    int maxIterations = 200;
    double targetExploitability = 0.5;
    int threads = 1;
    QString algorithm = QStringLiteral("discounted_cfr");
    int printInterval = 10;
    int dumpRounds = 2;

    QJsonObject toJson() const;
    static bool fromJson(const QJsonObject &json,
                         SolveOptions *options,
                         QString *errorMessage = nullptr);
};

} // namespace GtoApi

#endif // GTO_API_SOLVE_OPTIONS_H