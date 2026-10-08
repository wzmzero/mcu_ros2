#include "main_window.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <limits>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), session_(this) {
  clock_.start();
  setWindowTitle("MCU ROS 2 · Topic / Service / Action");
  resize(1280, 900);
  setMinimumSize(1000, 760);
  auto root = new QWidget;
  auto layout = new QVBoxLayout(root);
  layout->setSpacing(8);
  setCentralWidget(root);
  auto title = new QLabel("MCU ROS 2 工作台");
  title->setStyleSheet("font-size:24px;font-weight:600;color:#163449;");
  layout->addWidget(title);
  auto controls = new QHBoxLayout;
  distro_ = new QLineEdit("jazzy");
  distro_->setMaximumWidth(100);
  wsl_ = new QLineEdit;
  wsl_->setPlaceholderText("默认 WSL 发行版");
  wsl_->setMaximumWidth(170);
#ifndef Q_OS_WIN
  wsl_->hide();
#endif
  domain_ = new QSpinBox;
  domain_->setRange(0, 232);
  domain_->setObjectName("domain");
  localDds_ = new QCheckBox("DDS 仅本机");
  localDds_->setChecked(true);
  localDds_->setToolTip("与当前 WSL Agent 的 loopback 配置一致。跨电脑 DDS "
                        "时，Agent 和后端都需使用网络配置。");
  connect_ = new QPushButton("连接 ROS 2");
  connect_->setObjectName("connect");
  connection_ = new QLabel("未连接");
  controls->addWidget(new QLabel("ROS 2"));
  controls->addWidget(distro_);
  controls->addWidget(wsl_);
  controls->addWidget(new QLabel("Domain"));
  controls->addWidget(domain_);
  controls->addWidget(localDds_);
  controls->addWidget(connect_);
  controls->addWidget(connection_);
  controls->addStretch();
  layout->addLayout(controls);
  auto note =
      new QLabel("Agent 需单独运行；收到心跳后板卡才显示在线。板间通信由 ROS 2 "
                 "/ Agent 完成。");
  note->setStyleSheet("color:#526575;");
  layout->addWidget(note);
  telemetry_ = new QTableWidget(2, 10);
  telemetry_->setObjectName("telemetry");
  telemetry_->setHorizontalHeaderLabels(
      {"板卡", "状态", "Heartbeat", "Echo", "Peer RX", "Roundtrip", "Service",
       "Action FB", "Action值", "Action状态"});
  telemetry_->verticalHeader()->hide();
  telemetry_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  telemetry_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  telemetry_->setFixedHeight(105);
  for (int row = 0; row < 2; ++row)
    for (int col = 0; col < 10; ++col)
      telemetry_->setItem(row, col, new QTableWidgetItem("—"));
  layout->addWidget(telemetry_);
  auto tabs = new QTabWidget;
  layout->addWidget(tabs, 1);
  auto operations = new QWidget;
  auto ops = new QVBoxLayout(operations);
  ops->setSpacing(8);
  auto selection = new QHBoxLayout;
  board_ = new QComboBox;
  board_->setObjectName("board");
  board_->addItems({"esp32s3", "stm32"});
  selection->addWidget(new QLabel("目标板卡"));
  selection->addWidget(board_);
  selection->addStretch();
  ops->addLayout(selection);
  auto topicBox = new QGroupBox("Topic · 发布 command，板卡回传 echo");
  auto topic = new QHBoxLayout(topicBox);
  value_ = new QSpinBox;
  value_->setObjectName("commandValue");
  value_->setRange(std::numeric_limits<int>::min(),
                   std::numeric_limits<int>::max());
  value_->setValue(42);
  auto publish = new QPushButton("发布 Int32");
  publish->setObjectName("publish");
  topic->addWidget(new QLabel("数值"));
  topic->addWidget(value_);
  topic->addWidget(publish);
  topic->addStretch();
  ops->addWidget(topicBox);
  auto serviceBox = new QGroupBox("Service · AddTwoInts（有符号 64 位整数）");
  auto service = new QHBoxLayout(serviceBox);
  a_ = new QLineEdit("20");
  a_->setObjectName("operandA");
  b_ = new QLineEdit("22");
  b_->setObjectName("operandB");
  auto call = new QPushButton("请求求和");
  call->setObjectName("service");
  serviceResult_ = new QLabel("等待请求");
  service->addWidget(a_);
  service->addWidget(new QLabel("+"));
  service->addWidget(b_);
  service->addWidget(call);
  service->addWidget(serviceResult_);
  ops->addWidget(serviceBox);
  auto actionBox = new QGroupBox(
      "Action · Fibonacci（序列长度 2–10；其它长度用于测试拒绝）");
  auto action = new QVBoxLayout(actionBox);
  auto actionControls = new QHBoxLayout;
  order_ = new QSpinBox;
  order_->setRange(0, 100);
  order_->setValue(6);
  order_->setObjectName("order");
  auto send = new QPushButton("发送目标");
  send->setObjectName("action");
  auto cancel = new QPushButton("取消当前目标");
  cancel->setObjectName("cancel");
  actionControls->addWidget(new QLabel("长度"));
  actionControls->addWidget(order_);
  actionControls->addWidget(send);
  actionControls->addWidget(cancel);
  actionControls->addStretch();
  action->addLayout(actionControls);
  actionResult_ = new QLabel("等待目标");
  actionResult_->setObjectName("actionResult");
  actionResult_->setWordWrap(true);
  actionResult_->setMinimumHeight(65);
  action->addWidget(actionResult_);
  actionBox->setMinimumHeight(170);
  ops->addWidget(actionBox);
  ops->addStretch();
  auto operationsScroll = new QScrollArea;
  operationsScroll->setWidgetResizable(true);
  operationsScroll->setFrameShape(QFrame::NoFrame);
  operationsScroll->setWidget(operations);
  tabs->addTab(operationsScroll, "通信测试");
  auto sensorPage = new QWidget;
  auto sensorLayout = new QVBoxLayout(sensorPage);
  auto sensorControls = new QHBoxLayout;
  sensorTopic_ = new QComboBox;
  sensorTopic_->setObjectName("sensorTopic");
  sensorTopic_->setMinimumWidth(450);
  auto subscribe = new QPushButton("订阅传感器");
  subscribe->setObjectName("subscribe");
  sensorControls->addWidget(sensorTopic_, 1);
  sensorControls->addWidget(subscribe);
  sensorLayout->addLayout(sensorControls);
  sensorLayout->addWidget(
      new QLabel("自动列出 sensor_msgs "
                 "话题；当前固件没有物理传感器发布时列表为空。预览最多 10 "
                 "Hz，长数组显示前 64 项。"));
  sensorView_ = new QPlainTextEdit;
  sensorView_->setReadOnly(true);
  sensorView_->setObjectName("sensorView");
  sensorLayout->addWidget(sensorView_, 1);
  tabs->addTab(sensorPage, "传感器消息");
  auto graphPage = new QWidget;
  auto graphLayout = new QVBoxLayout(graphPage);
  auto refresh = new QPushButton("刷新 ROS 图");
  refresh->setObjectName("graph");
  graph_ = new QPlainTextEdit;
  graph_->setReadOnly(true);
  graphLayout->addWidget(refresh);
  graphLayout->addWidget(graph_);
  tabs->addTab(graphPage, "节点与话题");
  auto logBox = new QGroupBox("事件记录");
  auto logLayout = new QVBoxLayout(logBox);
  log_ = new QPlainTextEdit;
  log_->setObjectName("log");
  log_->setReadOnly(true);
  log_->setMaximumBlockCount(500);
  log_->setMinimumHeight(75);
  log_->setMaximumHeight(100);
  logLayout->addWidget(log_);
  auto logControls = new QHBoxLayout;
  auto clear = new QPushButton("清空");
  auto save = new QPushButton("导出日志");
  logControls->addStretch();
  logControls->addWidget(clear);
  logControls->addWidget(save);
  logLayout->addLayout(logControls);
  layout->addWidget(logBox);
  setStyleSheet(
      "QMainWindow {background:#f3f6fa;} QGroupBox {font-weight:600;border:1px "
      "solid #d6e0ea;border-radius:6px;margin-top:12px;padding:14px;} "
      "QGroupBox::title {subcontrol-origin:margin;left:12px;} QPushButton "
      "{padding:7px 14px;} QLineEdit,QSpinBox,QComboBox {padding:5px;} "
      "QPlainTextEdit,QTableWidget {background:white;} QTabWidget::pane "
      "{border:1px solid #d6e0ea;background:white;}");
  connect(connect_, &QPushButton::clicked, this, [this] {
    if (session_.active()) {
      session_.stop();
      return;
    }
    model_.clear();
    actionIds_.clear();
    actionLines_.clear();
    serviceResult_->setText("等待请求");
    actionResult_->setText("等待目标");
    session_.start({distro_->text().trimmed(), wsl_->text().trimmed(),
                    domain_->value(), localDds_->isChecked()});
    if (session_.active()) {
      connect_->setText("断开");
      connection_->setText("连接中…");
    }
  });
  connect(&session_, &RosSession::readyChanged, this, [this](bool ready) {
    connection_->setText(ready ? "ROS 已就绪" : "未连接");
    connect_->setText(ready ? "断开" : "连接 ROS 2");
    distro_->setEnabled(!ready);
    wsl_->setEnabled(!ready);
    domain_->setEnabled(!ready);
    localDds_->setEnabled(!ready);
    if (ready)
      session_.request({{"op", "graph"}});
    else {
      model_.clear();
      actionIds_.clear();
      refreshBoards();
    }
  });
  for (auto button : {publish, call, send, cancel, subscribe, refresh}) {
    button->setEnabled(false);
    connect(&session_, &RosSession::readyChanged, button,
            &QPushButton::setEnabled);
  }
  connect(publish, &QPushButton::clicked, this, [this] {
    session_.request({{"op", "publish"},
                      {"board", selectedBoard()},
                      {"value", value_->value()}});
  });
  connect(call, &QPushButton::clicked, this, [this] {
    bool okA, okB;
    a_->text().toLongLong(&okA);
    b_->text().toLongLong(&okB);
    if (!okA || !okB) {
      serviceResult_->setText("请输入有效的 Int64 整数");
      return;
    }
    if (!session_
             .request({{"op", "service"},
                       {"board", selectedBoard()},
                       {"a", a_->text().trimmed()},
                       {"b", b_->text().trimmed()}})
             .isEmpty())
      serviceResult_->setText("请求中…");
  });
  connect(send, &QPushButton::clicked, this, [this] {
    const auto board = selectedBoard();
    if (actionIds_.contains(board)) {
      appendLog(board + " 已有进行中的目标");
      return;
    }
    auto id = session_.request(
        {{"op", "action"}, {"board", board}, {"order", order_->value()}});
    if (!id.isEmpty()) {
      actionIds_[board] = id;
      actionLines_.clear();
      actionResult_->setText(board + "：等待目标响应…");
    }
  });
  connect(cancel, &QPushButton::clicked, this, [this] {
    auto id = actionIds_.value(selectedBoard());
    if (id.isEmpty()) {
      appendLog("该板卡没有进行中的目标");
      return;
    }
    session_.request(
        {{"op", "cancel"}, {"board", selectedBoard()}, {"goal_id", id}});
  });
  connect(refresh, &QPushButton::clicked, this,
          [this] { session_.request({{"op", "graph"}}); });
  connect(subscribe, &QPushButton::clicked, this, [this] {
    auto data = sensorTopic_->currentData().toJsonObject();
    if (data.isEmpty()) {
      appendLog("未发现 sensor_msgs 话题，请先刷新 ROS 图。");
      return;
    }
    session_.request({{"op", "subscribe"},
                      {"topic", data["topic"]},
                      {"type", data["type"]}});
  });
  connect(clear, &QPushButton::clicked, log_, &QPlainTextEdit::clear);
  connect(save, &QPushButton::clicked, this, [this] {
    auto path = QFileDialog::getSaveFileName(this, "导出事件记录",
                                             "qt_ros2.log", "日志 (*.log)");
    if (path.isEmpty())
      return;
    QSaveFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
      file.write(log_->toPlainText().toUtf8());
      if (!file.commit())
        appendLog("日志保存失败");
    } else
      appendLog("无法打开日志文件");
  });
  connect(&session_, &RosSession::eventReceived, this,
          &MainWindow::handleEvent);
  connect(&session_, &RosSession::logMessage, this, &MainWindow::appendLog);
  auto timer = new QTimer(this);
  connect(timer, &QTimer::timeout, this, &MainWindow::refreshBoards);
  timer->start(500);
  refreshBoards();
  statusBar()->showMessage("Topic: /<board>/command → echo  |  Service: "
                           "add_two_ints  |  Action: fibonacci");
}
QString MainWindow::selectedBoard() const { return board_->currentText(); }
void MainWindow::appendLog(const QString &message) {
  if (!message.isEmpty())
    log_->appendPlainText(
        QDateTime::currentDateTime().toString("HH:mm:ss.zzz ") +
        message.left(4000));
}
void MainWindow::refreshBoards() {
  for (int row = 0; row < 2; ++row) {
    const std::string board = mcu::boards[row];
    auto online = session_.ready() && model_.online(board, elapsed());
    telemetry_->item(row, 0)->setText(QString::fromStdString(board));
    telemetry_->item(row, 1)->setText(online ? "在线" : "等待心跳");
    telemetry_->item(row, 1)->setForeground(online ? QColor("#087d65")
                                                   : QColor("#8a6974"));
    for (int col = 0; col < 8; ++col) {
      auto s = model_.sample(board, mcu::fields[col]);
      auto text = QString::fromStdString(s.value);
      telemetry_->item(row, col + 2)->setText(text.isEmpty() ? "—" : text);
      telemetry_->item(row, col + 2)
          ->setToolTip(
              s.seen_ms ? QString("%1 秒前收到")
                              .arg((elapsed() - s.seen_ms) / 1000.0, 0, 'f', 1)
                        : "尚未收到");
    }
  }
}
void MainWindow::handleEvent(const QJsonObject &e) {
  auto event = e["event"].toString();
  if (event == "telemetry") {
    model_.update(e["board"].toString().toStdString(),
                  e["field"].toString().toStdString(),
                  e["value"].toString().toStdString(), elapsed());
    refreshBoards();
  } else if (event == "sensor")
    sensorView_->setPlainText(
        e["topic"].toString() + "\n" + e["type"].toString() + "\n" +
        QString::fromUtf8(QJsonDocument(e["data"].toObject())
                              .toJson(QJsonDocument::Indented)));
  else if (event == "action_feedback" || event == "action_accepted") {
    auto sequence = QString::fromUtf8(
        QJsonDocument(e["sequence"].toArray()).toJson(QJsonDocument::Compact));
    QString board;
    for (auto it = actionIds_.cbegin(); it != actionIds_.cend(); ++it)
      if (it.value() == e["id"].toString())
        board = it.key();
    auto line = board + (event == "action_accepted" ? "：目标已接受"
                                                    : "：反馈 " + sequence);
    actionLines_.append(line);
    while (actionLines_.size() > 4)
      actionLines_.removeFirst();
    actionResult_->setText(actionLines_.join('\n'));
    appendLog(line);
  } else if (event == "response") {
    auto op = e["op"].toString();
    auto result = e["result"].toObject();
    auto ok = e["ok"].toBool();
    if (op == "graph" && ok) {
      graph_->setPlainText(QString::fromUtf8(
          QJsonDocument(result).toJson(QJsonDocument::Indented)));
      auto previous = sensorTopic_->currentText();
      sensorTopic_->clear();
      for (auto t : result["topics"].toArray()) {
        auto topic = t.toObject();
        for (auto type : topic["types"].toArray())
          if (type.toString().startsWith("sensor_msgs/msg/"))
            sensorTopic_->addItem(
                topic["name"].toString() + " · " + type.toString(),
                QJsonObject{{"topic", topic["name"]}, {"type", type}});
      }
      if (auto index = sensorTopic_->findText(previous); index >= 0)
        sensorTopic_->setCurrentIndex(index);
    }
    auto text = ok ? QString::fromUtf8(
                         QJsonDocument(result).toJson(QJsonDocument::Compact))
                   : e["error"].toString();
    if (op == "service")
      serviceResult_->setText(
          e["board"].toString() +
          (ok ? " = " + result["sum"].toString() : " 失败：" + text));
    if (op == "action") {
      QString board;
      for (auto it = actionIds_.begin(); it != actionIds_.end();) {
        if (it.value() == e["id"].toString()) {
          board = it.key();
          it = actionIds_.erase(it);
        } else
          ++it;
      }
      auto state = !ok                             ? "失败"
                   : !result["accepted"].toBool()  ? "拒绝"
                   : result["status"].toInt() == 4 ? "成功"
                   : result["status"].toInt() == 5 ? "已取消"
                                                   : "结束";
      actionResult_->setText(board + "：" + state + "\n" + text);
    }
    if (op != "graph" || !ok)
      appendLog(op + " #" + e["id"].toString() + " " +
                (ok ? "完成 " : "失败 ") + text);
  } else if (event == "ready")
    appendLog("ROS 后端已就绪；等待板卡数据。");
  emit observed(e);
}
