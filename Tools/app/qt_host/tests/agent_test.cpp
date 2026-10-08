// Drives the real Agent buttons on a spare UDP port; leaves port 8888
// untouched.
#include "main_window.h"
#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPushButton>
#include <QSaveFile>
#include <QSpinBox>
#include <QTimer>
#include <memory>

namespace {
struct AgentTest {
  MainWindow *window;
  QString reportPath;
  QTimer timer;
  QJsonArray checks;
  QString state = "connect";
  int firstPid = 0;
  qint64 started = 0;
  bool finished = false;
  void finish(const QString &error) {
    if (finished)
      return;
    finished = true;
    timer.stop();
    QJsonObject report{{"passed", error.isEmpty()},
                       {"error", error},
                       {"port", 18888},
                       {"checks", checks}};
    QDir().mkpath(QFileInfo(reportPath).absolutePath());
    QSaveFile file(reportPath);
    auto data = QJsonDocument(report).toJson(QJsonDocument::Indented);
    bool saved = file.open(QIODevice::WriteOnly) &&
                 file.write(data) == data.size() && file.commit();
    window->session().stop();
    qInfo().noquote() << QString::fromUtf8(data);
    int code = !saved ? 2 : error.isEmpty() ? 0 : 1;
    QTimer::singleShot(500, qApp, [code] { QCoreApplication::exit(code); });
  }
  void event(const QJsonObject &event) {
    if (finished || event["event"] != "agent")
      return;
    auto current = event["state"].toString();
    if (current == "error") {
      finish(event["error"].toString());
      return;
    }
    if ((state == "start" || state == "restart") && current == "running") {
      if (!event["owned"].toBool()) {
        finish("Spare port 18888 already has an external Agent");
        return;
      }
      int pid = event["pid"].toInt();
      if (state == "start") {
        firstPid = pid;
        checks.append(
            QJsonObject{{"op", "Qt starts real Agent and verifies XRCE"},
                        {"pid", pid},
                        {"passed", true}});
        state = "stop";
        auto button = window->findChild<QPushButton *>("agentStop");
        if (!button->isEnabled()) {
          finish("Owned Agent stop button disabled");
          return;
        }
        button->click();
      } else {
        checks.append(QJsonObject{{"op", "Qt restarts Agent"},
                                  {"pid", pid},
                                  {"passed", pid != firstPid}});
        if (pid == firstPid) {
          finish("Agent was not restarted");
          return;
        }
        state = "disconnect";
        window->findChild<QPushButton *>("connect")->click();
      }
    } else if (state == "stop" && current == "stopped") {
      checks.append(QJsonObject{{"op", "Qt stops its Agent"},
                                {"passed", !event["owned"].toBool()}});
      state = "restart";
      window->findChild<QPushButton *>("agentStart")->click();
    }
  }
  void tick() {
    if (finished)
      return;
    if (window->elapsed() - started > 30000) {
      finish("Qt Agent lifecycle timed out: " + state);
      return;
    }
    if (state == "connect" && window->session().ready()) {
      state = "start";
      window->findChild<QPushButton *>("agentStart")->click();
    } else if (state == "disconnect" && !window->session().active()) {
      checks.append(
          QJsonObject{{"op", "Disconnect closes worker and owned Agent"},
                      {"passed", true}});
      finish({});
    }
  }
};
} // namespace
void startAgentTest(MainWindow &window, const QString &report) {
  auto test = std::make_shared<AgentTest>();
  test->window = &window;
  test->reportPath = report;
  test->started = window.elapsed();
  window.findChild<QCheckBox *>("autoAgent")->setChecked(false);
  window.findChild<QSpinBox *>("agentPort")->setValue(18888);
  QObject::connect(&window, &MainWindow::observed, &window,
                   [test](const QJsonObject &e) { test->event(e); });
  QObject::connect(&test->timer, &QTimer::timeout, &window,
                   [test] { test->tick(); });
  test->timer.start(100);
  QTimer::singleShot(0, &window, [&window] {
    window.findChild<QPushButton *>("connect")->click();
  });
}
