#include "ros_session.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>

RosSession::RosSession(QObject *parent) : QObject(parent) {
  startupTimer_.setSingleShot(true);
  shutdownTimer_.setSingleShot(true);
  connect(&process_, &QProcess::readyReadStandardOutput, this,
          &RosSession::readOutput);
  connect(&process_, &QProcess::readyReadStandardError, this, [this] {
    emit logMessage(
        QString::fromUtf8(process_.readAllStandardError()).trimmed());
  });
  connect(&process_, &QProcess::errorOccurred, this,
          [this](QProcess::ProcessError error) {
            emit logMessage("后端进程错误: " + process_.errorString());
            if (error == QProcess::FailedToStart) {
              startupTimer_.stop();
              ready_ = false;
              emit readyChanged(false);
            }
          });
  connect(&process_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
          this, [this](int code, QProcess::ExitStatus) {
            readOutput();
            startupTimer_.stop();
            shutdownTimer_.stop();
            ready_ = false;
            emit readyChanged(false);
            emit logMessage(QString("ROS 后端已结束，退出码 %1").arg(code));
          });
  connect(&startupTimer_, &QTimer::timeout, this, [this] {
    emit logMessage("ROS 后端启动超时；检查 WSL、ROS 安装和所选发行版。");
    stop();
  });
  connect(&shutdownTimer_, &QTimer::timeout, this, [this] {
    if (active()) {
      process_.kill();
      emit logMessage("后端未及时退出，已终止进程。");
    }
  });
}
RosSession::~RosSession() {
  if (active()) {
    stop();
    if (!process_.waitForFinished(2500)) {
      process_.kill();
      process_.waitForFinished(1000);
    }
  }
}
void RosSession::start(const RosOptions &options) {
  if (active())
    return;
  QString script = QDir::fromNativeSeparators(
      QCoreApplication::applicationDirPath() + "/runtime/ros2/run_worker.sh");
  if (!QFileInfo::exists(script)) {
    emit logMessage("找不到 ROS 后端脚本: " + script);
    return;
  }
  if (!QRegularExpression("^[a-z]+$").match(options.distro).hasMatch() ||
      options.domain < 0 || options.domain > 232) {
    emit logMessage("无效的 ROS 发行版或 Domain ID。");
    return;
  }
  buffer_.clear();
  stopping_ = false;
  ready_ = false;
  QStringList args;
#ifdef Q_OS_WIN
  if (script.size() < 3 || script[1] != ':') {
    emit logMessage("后端需存放在 WSL 可访问的 Windows 本地磁盘上。");
    return;
  }
  script = "/mnt/" + script.left(1).toLower() + script.mid(2);
  if (!options.wslDistribution.isEmpty())
    args << "--distribution" << options.wslDistribution;
  args << "--exec" << "bash" << script;
  process_.setProgram("wsl.exe");
#else
  args << script;
  process_.setProgram("/bin/bash");
#endif
  args << options.distro << QString::number(options.domain)
       << (options.localDds ? "1" : "0");
  process_.setArguments(args);
  process_.setProcessChannelMode(QProcess::SeparateChannels);
  process_.start();
  startupTimer_.start(20000);
  emit logMessage(QString("正在连接 ROS 2 %1，Domain %2")
                      .arg(options.distro)
                      .arg(options.domain));
}
QString RosSession::request(QJsonObject command) {
  if (!ready_ || stopping_) {
    emit logMessage("ROS 后端尚未就绪。");
    return {};
  }
  const QString id = QString::number(++serial_);
  command["id"] = id;
  const auto data =
      QJsonDocument(command).toJson(QJsonDocument::Compact) + '\n';
  if (process_.bytesToWrite() > 1024 * 1024 || process_.write(data) < 0) {
    emit logMessage("后端命令队列已满或连接中断。");
    return {};
  }
  return id;
}
void RosSession::stop() {
  if (!active() || stopping_)
    return;
  if (ready_)
    request({{"op", "stop"}});
  stopping_ = true;
  ready_ = false;
  startupTimer_.stop();
  process_.closeWriteChannel();
  shutdownTimer_.start(2000);
  emit readyChanged(false);
}
void RosSession::readOutput() {
  buffer_ += process_.readAllStandardOutput();
  if (buffer_.size() > 2 * 1024 * 1024) {
    buffer_.clear();
    emit logMessage("后端输出超过消息长度限制。");
    return;
  }
  int newline;
  while ((newline = buffer_.indexOf('\n')) >= 0) {
    const auto line = buffer_.left(newline);
    buffer_.remove(0, newline + 1);
    QJsonParseError error;
    auto doc = QJsonDocument::fromJson(line, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
      emit logMessage(QString::fromUtf8(line).left(1000));
      continue;
    }
    const auto event = doc.object();
    if (event["event"] == "ready") {
      if (stopping_)
        continue;
      ready_ = true;
      startupTimer_.stop();
      emit readyChanged(true);
    }
    emit eventReceived(event);
  }
}
