#ifndef GTO_API_GUI_SOLVER_JOB_RUNNER_H
#define GTO_API_GUI_SOLVER_JOB_RUNNER_H

#include "../services/solver_job_runner.h"

class QSolverJob;

namespace GtoApi {

class GuiSolverJobRunner : public SolverJobRunner
{
    Q_OBJECT

public:
    explicit GuiSolverJobRunner(QSolverJob *solverJob, QObject *parent = nullptr);

    bool start(const SpotSpec &spot,
               const SolveOptions &options,
               QString *errorMessage = nullptr) override;
    void cancel() override;

private:
    QSolverJob *m_solverJob = nullptr;
};

} // namespace GtoApi

#endif // GTO_API_GUI_SOLVER_JOB_RUNNER_H