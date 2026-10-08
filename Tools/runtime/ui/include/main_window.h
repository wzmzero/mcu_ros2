#pragma once
#include "board_model.h"
#include "ros_session.h"
#include <QElapsedTimer>
#include <QMainWindow>
#include <QMap>

class QComboBox;
class QSpinBox;
class QCheckBox;
class QLineEdit;
class QLabel;
class QPushButton;
class QPlainTextEdit;
class QTableWidget;
class MainWindow : public QMainWindow {
  Q_OBJECT
public:
  explicit MainWindow(QWidget *parent = nullptr);
  RosSession &session() { return session_; }
  const mcu::BoardModel &model() const { return model_; }
  qint64 elapsed() const { return clock_.elapsed() + 1; }
  QString selectedBoard() const;
signals:
  void observed(const QJsonObject &event);

private:
  RosSession session_;
  mcu::BoardModel model_;
  QElapsedTimer clock_;
  QComboBox *board_, *sensorTopic_;
  QSpinBox *domain_, *value_, *order_, *agentPort_;
  QLineEdit *distro_, *wsl_, *a_, *b_;
  QCheckBox *localDds_, *autoAgent_;
  QPushButton *connect_, *agentStart_, *agentStop_;
  QLabel *connection_, *serviceResult_, *actionResult_, *agentState_;
  QTableWidget *telemetry_;
  QPlainTextEdit *log_, *sensorView_, *graph_;
  QMap<QString, QString> actionIds_;
  QStringList actionLines_;
  QJsonObject agentInfo_;
  void handleEvent(const QJsonObject &event);
  void refreshBoards();
  void appendLog(const QString &message);
};
