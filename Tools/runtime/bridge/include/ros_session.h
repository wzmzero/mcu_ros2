#pragma once
#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QTimer>

struct RosOptions {
  QString distro = "jazzy";
  QString wslDistribution;
  int domain = 0;
  bool localDds = true;
};
class RosSession : public QObject {
  Q_OBJECT
public:
  explicit RosSession(QObject *parent = nullptr);
  ~RosSession() override;
  void start(const RosOptions &options);
  void stop();
  QString request(QJsonObject command);
  bool ready() const { return ready_; }
  bool active() const { return process_.state() != QProcess::NotRunning; }
signals:
  void eventReceived(const QJsonObject &event);
  void logMessage(const QString &text);
  void readyChanged(bool ready);

private:
  QProcess process_;
  QTimer startupTimer_, shutdownTimer_;
  QByteArray buffer_;
  bool ready_ = false;
  bool stopping_ = false;
  quint64 serial_ = 0;
  void readOutput();
};
