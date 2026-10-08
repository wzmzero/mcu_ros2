// Opt-in integration test: drives actual widgets, the native process bridge,
// WSL/local rclpy and real MCU endpoints. No mock MCU and no topic relay.
#include "main_window.h"
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QSet>
#include <QSpinBox>
#include <QTimer>
#include <memory>

namespace {
struct Operation {
  QString board, op;
  int value;
};
struct Smoke {
  MainWindow *window;
  QString reportPath, screenshotPath;
  QTimer timer;
  QJsonObject report{{"passed", false}, {"topic_forwarding", false}};
  QJsonArray checks;
  QJsonObject agent;
  QMap<QString, QMap<QString, QSet<QString>>> samples;
  QList<Operation> operations;
  int index = -1;
  qint64 start = 0, stageStart = 0;
  bool canceled = false, finished = false, screenshotSaved = false;
  bool sensorTest = false;
  QString tail;

  void finish(const QString &error) {
    if (finished)
      return;
    finished = true;
    timer.stop();
    QString finalError = error;
    if (!screenshotPath.isEmpty() && !screenshotSaved) {
      QDir().mkpath(QFileInfo(screenshotPath).absolutePath());
      screenshotSaved = window->grab().save(screenshotPath);
      report["screenshot_saved"] = screenshotSaved;
      if (!screenshotSaved && finalError.isEmpty())
        finalError = "Screenshot could not be saved";
    }
    report["passed"] = finalError.isEmpty();
    report["error"] = finalError;
    report["checks"] = checks;
    report["duration_ms"] = window->elapsed() - start;
    QJsonObject observations;
    for (auto board : {QString("esp32s3"), QString("stm32")}) {
      QJsonObject fields;
      for (auto it = samples[board].cbegin(); it != samples[board].cend();
           ++it) {
        QJsonArray values;
        for (const auto &value : it.value())
          values.append(value);
        fields[it.key()] = values;
      }
      observations[board] = fields;
    }
    report["observations"] = observations;
    QDir().mkpath(QFileInfo(reportPath).absolutePath());
    QSaveFile file(reportPath);
    const auto data = QJsonDocument(report).toJson(QJsonDocument::Indented);
    bool written = file.open(QIODevice::WriteOnly) &&
                   file.write(data) == data.size() && file.commit();
    window->session().stop();
    qInfo().noquote() << QString::fromUtf8(data);
    const int code = !written ? 2 : finalError.isEmpty() ? 0 : 1;
    QTimer::singleShot(500, qApp, [code] { QCoreApplication::exit(code); });
  }
  void advance() {
    ++index;
    stageStart = window->elapsed();
    canceled = false;
    if (index >= operations.size()) {
      if (sensorTest) {
        tail = "sensor_graph";
        window->session().request({{"op", "graph"}});
      } else
        beginReconnect();
      return;
    }
    auto operation = operations[index];
    window->findChild<QComboBox *>("board")->setCurrentText(operation.board);
    if (operation.op == "publish") {
      window->findChild<QSpinBox *>("commandValue")->setValue(operation.value);
      window->findChild<QPushButton *>("publish")->click();
    } else if (operation.op == "service") {
      window->findChild<QLineEdit *>("operandA")->setText("9007199254740993");
      window->findChild<QLineEdit *>("operandB")->setText("9");
      window->findChild<QPushButton *>("service")->click();
    } else {
      window->findChild<QSpinBox *>("order")->setValue(operation.value);
      window->findChild<QPushButton *>("action")->click();
    }
  }
  void beginReconnect() {
    if (!screenshotPath.isEmpty()) {
      QDir().mkpath(QFileInfo(screenshotPath).absolutePath());
      screenshotSaved = window->grab().save(screenshotPath);
      report["screenshot_saved"] = screenshotSaved;
      if (!screenshotSaved) {
        finish("Screenshot could not be saved");
        return;
      }
    }
    tail = "disconnect";
    stageStart = window->elapsed();
    window->findChild<QPushButton *>("connect")->click();
  }
  void event(const QJsonObject &e) {
    if (finished)
      return;
    auto event = e["event"].toString();
    if (event == "agent")
      agent = e;
    if (event == "telemetry") {
      auto &values = samples[e["board"].toString()][e["field"].toString()];
      values.insert(e["value"].toString());
      if (values.size() > 128)
        values.erase(values.begin());
    }
    if (index >= operations.size()) {
      if (tail == "sensor_graph" && event == "response" && e["op"] == "graph") {
        auto combo = window->findChild<QComboBox *>("sensorTopic");
        int chosen = -1;
        for (int i = 0; i < combo->count(); ++i)
          if (combo->itemData(i).toJsonObject()["topic"] ==
              "/qt_ros2_test/temperature")
            chosen = i;
        if (chosen < 0) {
          finish("Sensor test fixture was not discovered");
          return;
        }
        combo->setCurrentIndex(chosen);
        tail = "sensor";
        window->findChild<QPushButton *>("subscribe")->click();
      } else if (tail == "sensor" && event == "sensor") {
        bool passed = e["data"].toObject()["temperature"].toDouble() == 25.5 &&
                      window->findChild<QPlainTextEdit *>("sensorView")
                          ->toPlainText()
                          .contains("25.5");
        checks.append(QJsonObject{
            {"op", "standard_sensor_preview"},
            {"fixture", "host-generated Temperature, not MCU hardware"},
            {"passed", passed}});
        if (!passed) {
          finish("Sensor preview mismatch");
          return;
        }
        beginReconnect();
      }
      return;
    }
    if (index < 0)
      return;
    const auto current = operations[index];
    if (current.op == "publish" && event == "telemetry" &&
        e["board"] == current.board && e["field"] == "echo" &&
        e["value"].toString() == QString::number(current.value)) {
      checks.append(QJsonObject{{"board", current.board},
                                {"op", "command_echo"},
                                {"value", current.value},
                                {"passed", true}});
      advance();
      return;
    }
    if (current.op == "cancel" && event == "action_feedback") {
      if (!canceled && e["sequence"].toArray().size() >= 3) {
        canceled = true;
        window->findChild<QPushButton *>("cancel")->click();
      }
      return;
    }
    if (event != "response" || e["op"] == "graph" || e["op"] == "publish" ||
        e["op"] == "cancel")
      return;
    if (e["op"] != (current.op == "service" ? "service" : "action"))
      return;
    if (!e["ok"].toBool()) {
      finish(e["error"].toString());
      return;
    }
    auto result = e["result"].toObject();
    bool passed = false;
    if (current.op == "service")
      passed = result["sum"].toString() == "9007199254741002";
    else if (current.op == "reject")
      passed = !result["accepted"].toBool();
    else if (current.op == "action")
      passed = result["status"].toInt() == 4 &&
               result["sequence"].toArray() == QJsonArray{0, 1, 1, 2, 3, 5};
    else if (current.op == "cancel")
      passed = canceled && result["status"].toInt() == 5 &&
               result["sequence"].toArray().size() < 10;
    checks.append(QJsonObject{{"board", current.board},
                              {"op", current.op},
                              {"result", result},
                              {"passed", passed}});
    if (!passed) {
      finish("Unexpected " + current.op + " result from " + current.board);
      return;
    }
    advance();
  }
  void tick() {
    if (finished)
      return;
    auto now = window->elapsed();
    if (now - start > 100000) {
      finish("Hardware smoke test timed out");
      return;
    }
    if (index >= 0 && now - stageStart > 15000) {
      finish("Operation timed out: " +
             (index < operations.size() ? operations[index].op : tail));
      return;
    }
    if (tail == "disconnect" && !window->session().active()) {
      tail = "reconnect";
      window->findChild<QPushButton *>("connect")->click();
      return;
    }
    if (tail == "reconnect" && window->session().ready() &&
        window->model().online("esp32s3", now) &&
        window->model().online("stm32", now)) {
      checks.append(
          QJsonObject{{"op", "disconnect_reconnect"}, {"passed", true}});
      finish({});
      return;
    }
    if (index < 0 && window->session().ready()) {
      bool good = true;
      for (const auto &board : {QString("esp32s3"), QString("stm32")}) {
        auto peer = board == "esp32s3" ? "stm32" : "esp32s3";
        auto confirmed = samples[board]["heartbeat"];
        confirmed.intersect(samples[peer]["peer_received"]);
        confirmed.intersect(samples[board]["roundtrip"]);
        good = good && confirmed.size() >= 3 &&
               window->model().online(board.toStdString(), now);
      }
      if (good && agent["verified"].toBool()) {
        checks.append(QJsonObject{{"op", "automatic_agent_start_or_reuse"},
                                  {"owned", agent["owned"]},
                                  {"passed", true}});
        checks.append(
            QJsonObject{{"op", "native_peer_roundtrip"}, {"passed", true}});
        advance();
      }
    }
  }
};
} // namespace
void startSmokeTest(MainWindow &window, const QString &report,
                    const QString &screenshot, bool sensorTest) {
  auto smoke = std::make_shared<Smoke>();
  smoke->window = &window;
  smoke->reportPath = report;
  smoke->screenshotPath = screenshot;
  smoke->start = window.elapsed();
  smoke->sensorTest = sensorTest;
  for (auto board : {QString("esp32s3"), QString("stm32")}) {
    smoke->operations.append(
        {board, "publish", board == "esp32s3" ? 123456789 : 987654321});
    smoke->operations.append({board, "service", 0});
    smoke->operations.append({board, "action", 6});
    smoke->operations.append({board, "reject", 11});
    smoke->operations.append({board, "cancel", 10});
  }
  QObject::connect(&window, &MainWindow::observed, &window,
                   [smoke](const QJsonObject &e) { smoke->event(e); });
  QObject::connect(&smoke->timer, &QTimer::timeout, &window,
                   [smoke] { smoke->tick(); });
  smoke->timer.start(100);
  QTimer::singleShot(0, &window, [&window] {
    window.findChild<QPushButton *>("connect")->click();
  });
}
