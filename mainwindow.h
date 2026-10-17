#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTextEdit>
#include <QJsonValue>
#include <QPointer>
#include <QStringList>
#include "src/api/dto/spot_spec.h"
#include "src/api/services/gto_application_service.h"
#include <QPlainTextEdit>
#include "include/runtime/qsolverjob.h"
#include "qstextedit.h"
#include "strategyexplorer.h"
#include "rangeselector.h"
#include <QMessageBox>
#include "boardselector.h"
#include "settingeditor.h"
#include "include/ui/rangeselectortablemodel.h"
#include "include/ui/rangeselectortabledelegate.h"

namespace Ui {
class MainWindow;
}

class QTimer;
class QDockWidget;

namespace RangePilot {
class ChatPanel;
}

class MainWindow : public QMainWindow, public GtoApi::UiFocusController
{
    Q_OBJECT

public:
    static QSTextEdit * s_textEdit;
    explicit MainWindow(QWidget *parent = 0, GtoApi::GtoApplicationService *applicationService = nullptr);
    ~MainWindow();
    Ui::MainWindow * getPriUi();
    QSTextEdit * getLogArea();
    QSTextEdit * get_logwindow();

    bool focusNode(const QString &jobId, const QString &nodeId,
                   QString *errorCode = nullptr,
                   QString *errorMessage = nullptr) override;
    bool openResult(const QString &jobId,
                    QString *errorCode = nullptr,
                    QString *errorMessage = nullptr) override;

private slots:
    void on_buttomSolve_clicked();
    void on_clearLogButtom_clicked();
    void on_buildTreeButtom_clicked();
    void on_actionjson_triggered();
    void on_actionimport_triggered();
    void import_from_file(QString from);
    void on_actionexport_triggered();
    void on_actionSettings_triggered();
    void on_actionclear_all_triggered();
    void on_ip_range(QString range_text);
    void on_copyButtom_clicked();
    void on_showResultButton_clicked();
    void on_stopSolvingButton_clicked();
    void on_ipRangeSelectButtom_clicked();
    void on_oopRangeSelectButtom_clicked();
    void on_estimateMemoryButtom_clicked();
    void on_selectBoardButton_clicked();
    void on_openParametersFolderButton_clicked();
    void item_clicked(const QModelIndex&);

    void on_exportCurrentParameterButton_clicked();

    void on_ipRangeText_textChanged();

    void on_oopRangeText_textChanged();
    void onExpanded(const QModelIndex& index);
    void scheduleStateSync();
    void syncStateFromUi();
    void applySpotFromService(const GtoApi::SpotSpec &spot);
    void onStrategyNodeSelected(const QString &nodeId);

private:
    void clear_all_params();
    void setupChatPanel();
    StrategyExplorer *ensureStrategyExplorer(QString *errorMessage = nullptr);
    bool hasResultTree() const;
    GtoApi::SpotSpec currentSpotFromUi() const;
    QJsonValue currentTreeState() const;
    QJsonValue currentSolveState() const;
    Ui::MainWindow *ui = NULL;
    QSolverJob* qSolverJob = NULL;
    QFileSystemModel * qFileSystemModel = NULL;
    QPointer<StrategyExplorer> strategyExplorer;
    QPointer<QDockWidget> chatDock;
    QPointer<RangePilot::ChatPanel> chatPanel;
    RangeSelector* rangeSelector = NULL;
    boardselector* boardSelector = NULL;
    SettingEditor* settingEditor = NULL;
    RangeSelectorTableDelegate * ip_delegate;
    RangeSelectorTableDelegate * oop_delegate;
    RangeSelectorTableModel * ip_model;
    RangeSelectorTableModel * oop_model;
    GtoApi::GtoApplicationService * applicationService = nullptr;
    QTimer * stateSyncTimer = nullptr;
    QStringList currentSpotPositions = {QStringLiteral("OOP"), QStringLiteral("IP")};
    QString oopPosition = QStringLiteral("OOP");
    QString ipPosition = QStringLiteral("IP");
    bool applyingStateUpdate = false;
    QJsonValue currentSelection = QJsonValue::Null;

};

#endif // MAINWINDOW_H
