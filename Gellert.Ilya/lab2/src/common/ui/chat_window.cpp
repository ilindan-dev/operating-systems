#include "chat_window.hpp"

#include <QDateTime>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QScrollBar>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>

#include "ui/dialog_delegate.hpp"
#include "ui/message_delegate.hpp"
#include "ui/message_model.hpp"
#include "ui/theme.hpp"

namespace chat::ui {

namespace {

constexpr int kSidebarWidth = 300;
constexpr quint64 kCommonKey = 0;

QLabel* label(const QString& text, const char* objectName)
{
    auto* result = new QLabel(text);
    result->setObjectName(QString::fromLatin1(objectName));
    return result;
}

QString firstLine(const QString& text)
{
    return text.section(QLatin1Char('\n'), 0, 0);
}

} // namespace

ChatWindow::ChatWindow(ChatWindowConfig config, QWidget* parent)
    : QMainWindow(parent), config_(std::move(config))
{
    names_[config_.selfId] = config_.selfName;
    setWindowTitle(config_.windowTitle);
    resize(1080, 720);
    setMinimumSize(820, 520);

    auto* central = new QWidget;
    central->setObjectName(QStringLiteral("central"));
    auto* layout = new QHBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(buildSidebar());
    layout->addWidget(buildConversation(), 1);
    setCentralWidget(central);

    connection_ = new QLabel;
    counter_ = new QLabel;
    statusBar()->addWidget(connection_, 1);
    statusBar()->addPermanentWidget(counter_);
    statusBar()->setSizeGripEnabled(false);

    Dialog& common = createDialog(proto::kBroadcast, QStringLiteral("Общий чат"));
    common.preview = QStringLiteral("Сообщения для всех участников");
    refreshItem(common);
    dialogList_->setCurrentItem(common.item);

    setConnected(true, QStringLiteral("Подключение…"));
    updateCounter();
    showCurrentDialog();
}

QWidget* ChatWindow::buildSidebar()
{
    auto* sidebar = new QWidget;
    sidebar->setObjectName(QStringLiteral("sidebar"));
    sidebar->setAttribute(Qt::WA_StyledBackground);
    sidebar->setFixedWidth(kSidebarWidth);
    auto* layout = new QVBoxLayout(sidebar);
    layout->setContentsMargins(14, 18, 14, 14);
    layout->setSpacing(12);

    auto* brandRow = new QHBoxLayout;
    brandRow->addWidget(label(QStringLiteral("LocalChat"), "brand"));
    brandRow->addStretch();
    brandRow->addWidget(label(config_.transport.toUpper(), "badge"));
    layout->addLayout(brandRow);

    auto* card = new QFrame;
    card->setObjectName(QStringLiteral("selfCard"));
    auto* cardLayout = new QHBoxLayout(card);
    cardLayout->setContentsMargins(12, 12, 12, 12);
    cardLayout->setSpacing(12);
    selfAvatar_ = new QLabel;
    selfAvatar_->setFixedSize(44, 44);
    cardLayout->addWidget(selfAvatar_, 0, Qt::AlignTop);
    auto* cardText = new QVBoxLayout;
    cardText->setSpacing(2);
    selfName_ = label({}, "selfName");
    selfInfo_ = label({}, "selfInfo");
    selfInfo_->setWordWrap(true);
    selfInfo_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    cardText->addWidget(selfName_);
    cardText->addWidget(selfInfo_);
    cardLayout->addLayout(cardText, 1);
    layout->addWidget(card);
    setSelf(config_.selfId, config_.selfName, config_.info);

    layout->addSpacing(2);
    layout->addWidget(label(QStringLiteral("ДИАЛОГИ"), "sectionLabel"));

    dialogList_ = new QListWidget;
    dialogList_->setObjectName(QStringLiteral("dialogs"));
    dialogList_->setItemDelegate(new DialogDelegate(dialogList_));
    dialogList_->setMouseTracking(true);
    dialogList_->setFocusPolicy(Qt::NoFocus);
    dialogList_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    connect(dialogList_, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* item) {
        if (item != nullptr) {
            currentKey_ = item->data(dialog_role::kKey).toULongLong();
            showCurrentDialog();
        }
    });
    layout->addWidget(dialogList_, 1);

    auto* hint = label(config_.hint, "hint");
    hint->setWordWrap(true);
    layout->addWidget(hint);
    return sidebar;
}

QWidget* ChatWindow::buildConversation()
{
    auto* conversation = new QWidget;
    auto* layout = new QVBoxLayout(conversation);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* topBar = new QWidget;
    topBar->setObjectName(QStringLiteral("topBar"));
    topBar->setAttribute(Qt::WA_StyledBackground);
    auto* topLayout = new QHBoxLayout(topBar);
    topLayout->setContentsMargins(20, 12, 20, 12);
    topLayout->setSpacing(12);
    chatAvatar_ = new QLabel;
    chatAvatar_->setFixedSize(40, 40);
    topLayout->addWidget(chatAvatar_);
    auto* titles = new QVBoxLayout;
    titles->setSpacing(1);
    chatTitle_ = label({}, "chatTitle");
    chatSubtitle_ = label({}, "chatSubtitle");
    titles->addWidget(chatTitle_);
    titles->addWidget(chatSubtitle_);
    topLayout->addLayout(titles, 1);
    layout->addWidget(topBar);

    messages_ = new QListView;
    messages_->setObjectName(QStringLiteral("messages"));
    messages_->setItemDelegate(new MessageDelegate(messages_));
    messages_->setSelectionMode(QAbstractItemView::NoSelection);
    messages_->setFocusPolicy(Qt::NoFocus);
    messages_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    messages_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    messages_->setResizeMode(QListView::Adjust); // re-wrap bubbles when the window is resized
    messages_->setUniformItemSizes(false);
    messages_->verticalScrollBar()->setSingleStep(18);
    if (config_.canForward) {
        messages_->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(messages_, &QListView::customContextMenuRequested, this,
                &ChatWindow::showMessageMenu);
    }
    layout->addWidget(messages_, 1);

    auto* composer = new QWidget;
    composer->setObjectName(QStringLiteral("composer"));
    composer->setAttribute(Qt::WA_StyledBackground);
    auto* composerLayout = new QHBoxLayout(composer);
    composerLayout->setContentsMargins(20, 12, 20, 12);
    composerLayout->setSpacing(10);
    input_ = new QLineEdit;
    input_->setObjectName(QStringLiteral("input"));
    // A Cyrillic letter takes 2 bytes in UTF-8, the protocol allows kMaxTextSize bytes.
    input_->setMaxLength(static_cast<int>(proto::kMaxTextSize / 2));
    send_ = new QPushButton(QStringLiteral("Отправить"));
    send_->setObjectName(QStringLiteral("send"));
    send_->setCursor(Qt::PointingHandCursor);
    composerLayout->addWidget(input_, 1);
    composerLayout->addWidget(send_);
    layout->addWidget(composer);

    connect(input_, &QLineEdit::returnPressed, this, &ChatWindow::submit);
    connect(send_, &QPushButton::clicked, this, &ChatWindow::submit);
    connect(input_, &QLineEdit::textChanged, this, [this] { updateInput(); });
    return conversation;
}

void ChatWindow::setSelf(quint32 id, const QString& name, const QString& info)
{
    names_.remove(config_.selfId);
    config_.selfId = id;
    config_.selfName = name;
    names_[id] = name;
    selfAvatar_->setPixmap(avatar(name, id, 44));
    selfName_->setText(name);
    selfInfo_->setText(info);
}

QString ChatWindow::nameOf(quint32 id) const
{
    return names_.value(id, QStringLiteral("Участник %1").arg(id));
}

ChatWindow::Dialog& ChatWindow::createDialog(quint32 participant, const QString& name)
{
    const quint64 key = participant == proto::kBroadcast ? kCommonKey : nextKey_++;
    Dialog& dialog = dialogs_[key];
    dialog.key = key;
    dialog.participant = participant;
    dialog.name = name;
    dialog.model = new MessageModel(this);
    dialog.item = new QListWidgetItem(name);
    dialog.item->setData(dialog_role::kKey, key);
    dialog.item->setData(dialog_role::kParticipant, participant);
    dialogList_->addItem(dialog.item);
    if (participant != proto::kBroadcast) {
        activeDialogs_[participant] = key;
    }
    refreshItem(dialog);
    return dialog;
}

ChatWindow::Dialog* ChatWindow::activeDialogWith(quint32 participant)
{
    if (participant == proto::kBroadcast) {
        return &dialogs_.at(kCommonKey);
    }
    const auto it = activeDialogs_.constFind(participant);
    return it == activeDialogs_.constEnd() ? nullptr : &dialogs_.at(*it);
}

ChatWindow::Dialog& ChatWindow::currentDialog()
{
    return dialogs_.at(currentKey_);
}

void ChatWindow::refreshItem(const Dialog& dialog)
{
    dialog.item->setText(dialog.name);
    dialog.item->setData(dialog_role::kPreview, dialog.preview);
    dialog.item->setData(dialog_role::kTime, dialog.lastTimeMs);
    dialog.item->setData(dialog_role::kUnread, dialog.unread);
    dialog.item->setData(dialog_role::kOnline, dialog.online);
}

void ChatWindow::append(Dialog& dialog, ChatEntry entry, const QString& preview)
{
    const bool isCurrent = dialog.key == currentKey_;
    auto* bar = messages_->verticalScrollBar();
    const bool atBottom = bar->value() >= bar->maximum() - 8;
    const bool own = entry.own;
    const bool isMessage = entry.kind == ChatEntry::Kind::Message;
    const qint64 time = entry.timeMs;

    dialog.model->add(std::move(entry));
    if (time >= dialog.lastTimeMs) {
        dialog.preview = firstLine(preview);
        dialog.lastTimeMs = time;
    }
    if (!isCurrent && !own && isMessage) {
        ++dialog.unread;
    }
    refreshItem(dialog);

    if (isCurrent && (atBottom || own)) {
        QTimer::singleShot(0, messages_, [this] { messages_->scrollToBottom(); });
    }
}

void ChatWindow::addChatMessage(quint32 from, quint32 to, qint64 sentAtMs, const QString& text,
                                quint32 forwardedFrom)
{
    // A private message belongs to the dialog with the other party.
    const bool common = to == proto::kBroadcast;
    const quint32 other = from == config_.selfId ? to : from;
    Dialog* dialog = activeDialogWith(common ? proto::kBroadcast : other);
    if (dialog == nullptr) {
        dialog = &createDialog(other, nameOf(other));
    }

    ChatEntry entry;
    entry.timeMs = sentAtMs;
    entry.authorId = from;
    entry.own = from == config_.selfId;
    entry.author = entry.own ? QStringLiteral("Вы") : nameOf(from);
    entry.text = text;
    if (forwardedFrom != proto::kNobody) {
        entry.forwardedFromId = forwardedFrom;
        entry.forwardedFrom =
            forwardedFrom == config_.selfId ? QStringLiteral("вы") : nameOf(forwardedFrom);
    }

    QString preview = forwardedFrom != proto::kNobody ? QStringLiteral("↪ ") + text : text;
    if (entry.own) {
        preview = QStringLiteral("Вы: ") + preview;
    } else if (common) {
        preview = entry.author + QStringLiteral(": ") + preview;
    }
    append(*dialog, std::move(entry), preview);
}

void ChatWindow::addStatus(const QString& text, quint32 about, qint64 timeMs)
{
    ChatEntry entry;
    entry.kind = ChatEntry::Kind::Status;
    entry.timeMs = timeMs >= 0 ? timeMs : QDateTime::currentMSecsSinceEpoch();
    entry.text = text;

    if (Dialog* dialog = activeDialogWith(about); dialog != nullptr && dialog->key != kCommonKey) {
        append(*dialog, entry, text);
    }
    Dialog& common = dialogs_.at(kCommonKey);
    common.model->add(entry); // statuses do not count as unread messages
    if (currentKey_ == kCommonKey) {
        QTimer::singleShot(0, messages_, [this] { messages_->scrollToBottom(); });
    }
}

void ChatWindow::addParticipant(quint32 id, const QString& name, const QString& details)
{
    names_[id] = name;
    Dialog* dialog = activeDialogWith(id);
    if (dialog == nullptr) {
        dialog = &createDialog(id, name);
        dialog->preview = QStringLiteral("Нажмите, чтобы написать лично");
    }
    dialog->name = name;
    dialog->online = true;
    dialog->item->setToolTip(details);
    refreshItem(*dialog);
    updateCounter();
    if (dialog->key == currentKey_) {
        showCurrentDialog();
    }
}

void ChatWindow::removeParticipant(quint32 id)
{
    Dialog* dialog = activeDialogWith(id);
    if (dialog == nullptr) {
        return;
    }
    dialog->online = false;
    refreshItem(*dialog);
    activeDialogs_.remove(id); // the id may be given to someone else later
    updateCounter();
    if (dialog->key == currentKey_) {
        showCurrentDialog();
    }
}

void ChatWindow::openDialog(quint32 participant)
{
    if (Dialog* dialog = activeDialogWith(participant); dialog != nullptr) {
        dialogList_->setCurrentItem(dialog->item);
    }
}

void ChatWindow::showCurrentDialog()
{
    Dialog& dialog = currentDialog();
    dialog.unread = 0;
    refreshItem(dialog);
    messages_->setModel(dialog.model);

    if (dialog.key == kCommonKey) {
        chatAvatar_->setPixmap(commonChatAvatar(40));
        chatTitle_->setText(QStringLiteral("Общий чат"));
        chatSubtitle_->setText(QStringLiteral("Сообщения видят все участники"));
        input_->setPlaceholderText(QStringLiteral("Сообщение для всех…"));
    } else {
        chatAvatar_->setPixmap(avatar(dialog.name, dialog.participant, 40));
        chatTitle_->setText(dialog.name);
        if (dialog.online) {
            chatSubtitle_->setText(
                QStringLiteral("Личный диалог · сообщения видите только вы и %1").arg(dialog.name));
            input_->setPlaceholderText(QStringLiteral("Личное сообщение → %1").arg(dialog.name));
        } else {
            chatSubtitle_->setText(QStringLiteral("Участник вышел из чата"));
            input_->setPlaceholderText(QStringLiteral("%1 больше не в чате").arg(dialog.name));
        }
    }
    updateInput();
    QTimer::singleShot(0, messages_, [this] { messages_->scrollToBottom(); });
    input_->setFocus();
}

void ChatWindow::updateInput()
{
    const bool canWrite = connected_ && currentDialog().online;
    input_->setEnabled(canWrite);
    send_->setEnabled(canWrite && !input_->text().trimmed().isEmpty());
}

void ChatWindow::submit()
{
    const QString text = input_->text().trimmed();
    if (text.isEmpty() || !input_->isEnabled()) {
        return;
    }
    emit sendRequested(currentDialog().participant, text);
    input_->clear();
}

void ChatWindow::showMessageMenu(const QPoint& position)
{
    const QModelIndex index = messages_->indexAt(position);
    if (!index.isValid()) {
        return;
    }
    const auto entry = index.data(MessageModel::kEntryRole).value<ChatEntry>();
    if (entry.kind != ChatEntry::Kind::Message) {
        return;
    }
    // Forwarding a forwarded message keeps the original author.
    const quint32 author =
        entry.forwardedFromId != proto::kNobody ? entry.forwardedFromId : entry.authorId;

    QMenu menu(this);
    QAction* toCommon = menu.addAction(QStringLiteral("Переслать в общий чат"));
    toCommon->setEnabled(currentKey_ != kCommonKey);
    connect(toCommon, &QAction::triggered, this, [this, author, text = entry.text] {
        emit forwardRequested(author, text, proto::kBroadcast);
    });

    QMenu* privately = menu.addMenu(QStringLiteral("Переслать лично"));
    for (const auto& [key, dialog] : dialogs_) {
        if (key == kCommonKey || !dialog.online || key == currentKey_) {
            continue;
        }
        QAction* action =
            privately->addAction(QIcon(avatar(dialog.name, dialog.participant, 20)), dialog.name);
        const quint32 to = dialog.participant;
        connect(action, &QAction::triggered, this,
                [this, author, to, text = entry.text] { emit forwardRequested(author, text, to); });
    }
    privately->setEnabled(!privately->isEmpty());
    menu.exec(messages_->viewport()->mapToGlobal(position));
}

void ChatWindow::setConnected(bool connected, const QString& text)
{
    connected_ = connected;
    const QColor dot = connected ? palette::online : palette::offline;
    connection_->setText(
        QStringLiteral("<span style='color:%1'>●</span>&nbsp;&nbsp;%2").arg(dot.name(), text));
    updateInput();
}

void ChatWindow::updateCounter()
{
    counter_->setText(QStringLiteral("В чате: %1").arg(activeDialogs_.size() + 1)); // + you
}

} // namespace chat::ui
