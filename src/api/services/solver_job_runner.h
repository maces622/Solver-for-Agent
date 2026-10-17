#ifndef GTO_API_SOLVER_JOB_RUNNER_H
#define GTO_API_SOLVER_JOB_RUNNER_H

#include "../dto/solve_options.h"
#include "../dto/spot_spec.h"

#include <QObject>
#include <QString>

namespace GtoApi {

class SolverJobRunner : public QObject
{
    Q_OBJECT

public:
    explicit SolverJobRunner(QObject *parent = nullptr) : QObject(parent) {}
    ~SolverJobRunner() override = default;

    virtual bool start(const SpotSpec &spot,
                       const SolveOptions &options,
                       QString *errorMessage = nullptr) = 0;
    virtual void cancel() = 0;

signals:
    void progress(int iteration, int maxIterations, double exploitability);
    void completed();
    void failed(const QString &message);
    void cancelled();
};

} // namespace GtoApi

#endif // GTO_API_SOLVER_JOB_RUNNER_H