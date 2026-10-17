#include "gui_solver_job_runner.h"

#include "include/GameTree.h"
#include "include/runtime/qsolverjob.h"

namespace GtoApi {

namespace {

bool isOopPosition(const QString &position)
{
    const QString normalized = position.trimmed().toUpper();
    return normalized == QStringLiteral("OOP")
           || normalized == QStringLiteral("BB")
           || normalized == QStringLiteral("SB");
}

} // namespace

GuiSolverJobRunner::GuiSolverJobRunner(QSolverJob *solverJob, QObject *parent)
    : SolverJobRunner(parent)
    , m_solverJob(solverJob)
{
    if (!m_solverJob) {
        return;
    }
    connect(m_solverJob, &QSolverJob::jobProgress,
            this, &SolverJobRunner::progress);
    connect(m_solverJob, &QSolverJob::jobSucceeded,
            this, &SolverJobRunner::completed);
    connect(m_solverJob, &QSolverJob::jobFailed,
            this, &SolverJobRunner::failed);
    connect(m_solverJob, &QSolverJob::jobCancelled,
            this, &SolverJobRunner::cancelled);
}

bool GuiSolverJobRunner::start(const SpotSpec &spot,
                               const SolveOptions &options,
                               QString *errorMessage)
{
    if (!m_solverJob) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Solver job is not available");
        }
        return false;
    }
    if (m_solverJob->isRunning()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Solver worker is busy");
        }
        return false;
    }

    PokerSolver *solver = m_solverJob->get_solver();
    if (!solver || !solver->get_game_tree()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Build the game tree before starting solve");
        }
        return false;
    }

    QString oopRange;
    QString ipRange;
    bool hasOop = false;
    bool hasIp = false;
    for (const QString &position : spot.positions) {
        const QString range = spot.ranges.value(position).toString();
        if (isOopPosition(position)) {
            oopRange = range;
            hasOop = true;
        } else {
            ipRange = range;
            hasIp = true;
        }
    }
    if (!hasOop && !spot.positions.isEmpty()) {
        oopRange = spot.ranges.value(spot.positions.at(0)).toString();
    }
    if (!hasIp && spot.positions.size() > 1) {
        ipRange = spot.ranges.value(spot.positions.at(1)).toString();
    }

    m_solverJob->mode = spot.mode == QStringLiteral("shortdeck")
                            ? QSolverJob::Mode::SHORTDECK
                            : QSolverJob::Mode::HOLDEM;
    m_solverJob->range_oop = oopRange.toStdString();
    m_solverJob->range_ip = ipRange.toStdString();
    m_solverJob->board = spot.board.join(QLatin1Char(',')).toStdString();
    m_solverJob->max_iteration = options.maxIterations;
    m_solverJob->accuracy = static_cast<float>(options.targetExploitability);
    m_solverJob->print_interval = options.printInterval;
    m_solverJob->thread_number = options.threads;
    m_solverJob->use_isomorphism = spot.useIsomorphism ? 1 : 0;
    m_solverJob->use_halffloats = spot.useHalfFloats ? 1 : 0;
    m_solverJob->allin_threshold = static_cast<float>(spot.allinThreshold);
    m_solverJob->dump_rounds = options.dumpRounds;
    m_solverJob->current_mission = QSolverJob::MissionType::SOLVING;
    m_solverJob->resetCancel();
    m_solverJob->start();
    return true;
}

void GuiSolverJobRunner::cancel()
{
    if (m_solverJob) {
        m_solverJob->stop();
    }
}

} // namespace GtoApi