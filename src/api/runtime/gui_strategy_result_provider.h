#ifndef GTO_API_GUI_STRATEGY_RESULT_PROVIDER_H
#define GTO_API_GUI_STRATEGY_RESULT_PROVIDER_H

#include "../services/strategy_result_provider.h"

class QSolverJob;

namespace GtoApi {

class GuiStrategyResultProvider : public StrategyResultProvider
{
    Q_OBJECT

public:
    explicit GuiStrategyResultProvider(QSolverJob *solverJob,
                                       QObject *parent = nullptr);

    bool getTreeSummary(const QString &jobId,
                        ResultTreeSummary *summary,
                        QString *errorCode = nullptr,
                        QString *errorMessage = nullptr) override;
    bool queryNode(const QString &jobId,
                   const QString &nodeId,
                   const QStringList &actionFilter,
                   int limit,
                   NodeQueryResult *result,
                   QString *errorCode = nullptr,
                   QString *errorMessage = nullptr) override;

private:
    QSolverJob *m_solverJob = nullptr;
};

} // namespace GtoApi

#endif // GTO_API_GUI_STRATEGY_RESULT_PROVIDER_H