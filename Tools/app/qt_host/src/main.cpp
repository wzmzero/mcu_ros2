#include "main_window.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QFont>
#include <QPushButton>
#include <QTextStream>
#include <QTimer>

void startSmokeTest(MainWindow &window, const QString &report,
                    const QString &screenshot, bool sensorTest);
int main(int argc, char **argv) {
  QApplication app(argc, argv);
#ifdef Q_OS_WIN
  app.setFont(QFont("Microsoft YaHei UI", 10));
#endif
  QCoreApplication::setApplicationName("qt_ros2");
  QCoreApplication::setApplicationVersion("1.0");
  QCommandLineParser parser;
  parser.setApplicationDescription(
      "Native Qt ROS 2 MCU workstation (Windows + WSL / Linux).");
  parser.addHelpOption();
  parser.addVersionOption();
  parser.addOption({"smoke-test",
                    "Run real hardware tests through the UI and exit (Agent "
                    "and both boards required).",
                    "report.json"});
  parser.addOption({"screenshot",
                    "Save the live window screenshot during smoke test.",
                    "window.png"});
  parser.addOption({"sensor-test", "Also require the clearly labeled "
                                   "/qt_ros2_test/temperature test fixture."});
  parser.addOption(
      {"connect", "Connect automatically using default settings."});
  // QCommandLineParser::process() may display a modal help box on Windows.
  // Keep command-line/help checks usable from CTest and redirected processes.
  if (!parser.parse(app.arguments())) {
    QTextStream(stderr) << parser.errorText() << Qt::endl;
    return 2;
  }
  if (parser.isSet("help") || parser.isSet("help-all")) {
    QTextStream(stdout) << parser.helpText();
    return 0;
  }
  if (parser.isSet("version")) {
    QTextStream(stdout) << QCoreApplication::applicationVersion() << Qt::endl;
    return 0;
  }
  MainWindow window;
  window.show();
  if (parser.isSet("smoke-test"))
    startSmokeTest(window, parser.value("smoke-test"),
                   parser.value("screenshot"), parser.isSet("sensor-test"));
  else if (parser.isSet("connect"))
    QTimer::singleShot(0, &window, [&window] {
      window.findChild<QPushButton *>("connect")->click();
    });
  return app.exec();
}
