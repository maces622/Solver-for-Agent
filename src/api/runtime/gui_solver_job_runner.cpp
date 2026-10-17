#include "gui_solver_job_runner.h"

#include "include/GameTree.h"
#include "include/runtime/qsolverjob.h"
#include "include/tools/GameTreeBuildingSettings.h"

#include <QJsonArray>
#include <QJsonObject>

namespace GtoApi {

namespace {

bool isOopPosition(const QString &position)
{
    const QString normalized = position.trimmed().toUpper();
    return normalized == QStringLiteral("OOP")
           || normalized == QStringLiteral("BB")
           || normalized == QStringLiteral("SB");
}

std::vector<float> sizesFromJson(const QJsonObject &side, const QString &key)
{
    std::vector<float> result;
    for (const QJsonValue &value : side.value(key).toArray()) {
        if (value.isDouble()) {
            result.push_back(static_cast<float>(value.toDouble()));
        }
    }
    return result;
}

StreetSetting streetSetting(const QJsonObject &street,
                            const QString &sideKey,
                            bool includeDonk)
{
    const QJsonObject side = street.value(sideKey).toObject();
    return StreetSetting(sizesFromJson(side, QStringLiteral("bet")),
                         sizesFromJson(side, QStringLiteral("raise")),
                         includeDonk ? sizesFromJson(side, QStringLiteral("donk"))
                                     : std::vector<float>(),
                         side.value(QStringLiteral("allin")).toBool());
}

void configureTreeBuilding(QSolverJob *job, const SpotSpec &spot)
{
    const int boardCards = spot.board.size();
    job->current_round = boardCards >= 5 ? 3 : (boardCards == 4 ? 2 : 1);
    job->raise_limit = 3;
    job->ip_commit = static_cast<float>(spot.pot / 2.0);
    job->oop_commit = static_cast<float>(spot.pot / 2.0);
    job->stack = static_cast<float>(spot.effectiveStackBb) + job->ip_commit;
    if (spot.blinds.size() >= 2) {
        job->small_blind = static_cast<float>(spot.blinds.at(0));
        job->big_blind = static_cast<float>(spot.blinds.at(1));
    }

    const QJsonObject sizing = spot.betSizing;
    const QJsonObject flop = sizing.value(QStringLiteral("flop")).toObject();
    const QJsonObject turn = sizing.value(QStringLiteral("turn")).toObject();
    const QJsonObject river = sizing.value(QStringLiteral("river")).toObject();
    job->gtbs = make_shared<GameTreeBuildingSettings>(
        streetSetting(flop, QStringLiteral("ip"), false),
        streetSetting(turn, QStringLiteral("ip"), false),
        streetSetting(river, QStringLiteral("ip"), false),
        streetSetting(flop, QStringLiteral("oop"), false),
        streetSetting(turn, QStringLiteral("oop"), true),
        streetSetting(river, QStringLiteral("oop"), true));
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
    if (!solver) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Solver is unavailable");
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
    configureTreeBuilding(m_solverJob, spot);
    // API solve.start owns the complete "prepare current spot, then solve"
    // operation. The GUI's separate Build Tree button is a UI convenience,
    // not a precondition of the public API.
    m_solverJob->current_mission = QSolverJob::MissionType::BUILDTREE_AND_SOLVE;
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