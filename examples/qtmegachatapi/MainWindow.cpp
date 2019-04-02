#include "MainWindow.h"
#include "ui_MainWindow.h"
#include <QInputDialog>
#include <QMessageBox>
#include <QMenu>
#include <QTMegaChatEvent.h>
#include "uiSettings.h"
#include "chatSettings.h"

using namespace mega;
using namespace megachat;

MainWindow::MainWindow(QWidget *parent, MegaLoggerApplication *logger, megachat::MegaChatApi *megaChatApi, ::mega::MegaApi *megaApi) :
    QMainWindow(0),
    ui(new Ui::MainWindow)
{
    mApp = (MegaChatApplication *) parent;
    mNContacts = 0;
    mActiveChats = 0;
    mArchivedChats = 0;
    mInactiveChats = 0;
    ui->setupUi(this);
    ui->contactList->setSelectionMode(QAbstractItemView::NoSelection);
    ui->chatList->setSelectionMode(QAbstractItemView::NoSelection);
    mMegaChatApi = megaChatApi;
    mMegaApi = megaApi;
    onlineStatus = NULL;
    mShowArchived = false;
    mLogger = logger;
    mChatSettings = new ChatSettings();
    qApp->installEventFilter(this);

    megaChatListenerDelegate = new QTMegaChatListener(mMegaChatApi, this);
    mMegaChatApi->addChatListener(megaChatListenerDelegate);
#ifndef KARERE_DISABLE_WEBRTC
    megaChatCallListenerDelegate = new megachat::QTMegaChatCallListener(mMegaChatApi, this);
    mMegaChatApi->addChatCallListener(megaChatCallListenerDelegate);
#endif
}

MainWindow::~MainWindow()
{
    removeListeners();
    delete mChatSettings;
    clearChatControllers();
    clearContactControllersMap();
    delete ui;
}

void MainWindow::removeListeners()
{
    if (megaChatListenerDelegate)
    {
        mMegaChatApi->removeChatListener(megaChatListenerDelegate);
        delete megaChatListenerDelegate;
        megaChatListenerDelegate = NULL;
    }

    #ifndef KARERE_DISABLE_WEBRTC
    if (megaChatCallListenerDelegate)
    {
        mMegaChatApi->removeChatCallListener(megaChatCallListenerDelegate);
        delete megaChatCallListenerDelegate;
        megaChatCallListenerDelegate = NULL;
    }
    #endif
}


void MainWindow::clearContactControllersMap()
{
    std::map<mega::MegaHandle, ContactListItemController *>::iterator it;
    for (it = mContactControllers.begin(); it != mContactControllers.end(); it++)
    {
        ContactListItemController *itemController = it->second;
        delete itemController;
    }

    mContactControllers.clear();
}

std::string MainWindow::getAuthCode()
{
    bool ok;
    QString qCode;

    while (1)
    {
        qCode = QInputDialog::getText((QWidget *)this, tr("Login verification"),
                tr("Enter the 6-digit code generated by your authenticator app"), QLineEdit::Normal, "", &ok);

        if (ok)
        {
            if (qCode.size() == 6)
            {
                return qCode.toStdString();
            }
        }
        else
        {
            return "";
        }
    }
}

void MainWindow::onTwoFactorCheck()
{
    mMegaApi->multiFactorAuthCheck(mMegaChatApi->getMyEmail());
}

void MainWindow::onTwoFactorGetCode()
{
    mMegaApi->multiFactorAuthGetCode();
}

void MainWindow::onTwoFactorDisable()
{
    std::string auxcode = getAuthCode();
    if (!auxcode.empty())
    {
        QString code(auxcode.c_str());
        mMegaApi->multiFactorAuthDisable(code.toUtf8().constData());
    }
}

void MainWindow::createFactorMenu(bool factorEnabled)
{
    QMenu menu(this);
    if(factorEnabled)
    {
        auto disableFA = menu.addAction("Disable 2FA");
        connect(disableFA, SIGNAL(triggered()), this, SLOT(onTwoFactorDisable()));
    }
    else
    {
        auto getFA = menu.addAction("Enable 2FA");
        connect(getFA, SIGNAL(triggered()), this, SLOT(onTwoFactorGetCode()));
    }

    menu.setLayoutDirection(Qt::RightToLeft);
    menu.adjustSize();
    menu.exec(ui->bSettings->mapToGlobal(
        QPoint(-menu.width()+ui->bSettings->width(), ui->bSettings->height())));

    menu.deleteLater();    
}

#ifndef KARERE_DISABLE_WEBRTC
void MainWindow::onChatCallUpdate(megachat::MegaChatApi */*api*/, megachat::MegaChatCall *call)
{
    ChatListItemController *itemController = getChatControllerById(call->getChatid());
    if (!itemController)
    {
        if (call->getStatus() > MegaChatCall::CALL_STATUS_IN_PROGRESS)
        {
            // It's a valid condition if we have been removed from the chatroom and call is being destroyed
            return;
        }

        throw std::runtime_error("Incoming call from unknown contact");
    }

    ChatWindow *window = itemController->showChatWindow();
    assert(window);

    if (call->hasChanged(MegaChatCall::CHANGE_TYPE_STATUS))
    {
        switch (call->getStatus())
        {
            case megachat::MegaChatCall::CALL_STATUS_TERMINATING_USER_PARTICIPATION:
            {
                window->hangCall();
                return;
            }
            case megachat::MegaChatCall::CALL_STATUS_RING_IN:
            {
                std::set<CallGui *> *setCallGui = window->getCallGui();

                if (setCallGui->size() == 0)
                {
                    window->createCallGui(call->hasVideoInitialCall(), mMegaChatApi->getMyUserHandle(), mMegaChatApi->getMyClientidHandle(call->getChatid()));
                }
                break;
            }
            case megachat::MegaChatCall::CALL_STATUS_IN_PROGRESS:
            {
                std::set<CallGui *> *setOfCallGui = window->getCallGui();

                if (setOfCallGui->size() != 0)
                {
                    window->connectPeerCallGui(mMegaChatApi->getMyUserHandle(), mMegaChatApi->getMyClientidHandle(call->getChatid()));
                }

                break;
            }
        }
    }

    if (call->hasChanged(MegaChatCall::CHANGE_TYPE_REMOTE_AVFLAGS) &&
            call->getStatus() == megachat::MegaChatCall::CALL_STATUS_IN_PROGRESS)
    {
        std::set<CallGui *> *setOfCallGui = window->getCallGui();
        std::set<CallGui *>::iterator it;
        for (it = setOfCallGui->begin(); it != setOfCallGui->end(); ++it)
        {
            CallGui *callGui = *it;
            MegaChatHandle peerid = call->getPeerSessionStatusChange();
            MegaChatHandle clientid = call->getClientidSessionStatusChange();
            if (callGui->getPeerid() == peerid && callGui->getClientid() == clientid)
            {
                MegaChatSession *session = call->getMegaChatSession(peerid, clientid);
                if (session->hasVideo())
                {
                    callGui->ui->videoRenderer->disableStaticImage();
                }
                else
                {
                    callGui->setAvatar();
                    callGui->ui->videoRenderer->enableStaticImage();
                }
                break;
            }
        }
    }

    //NEW SESSIONS
    if (call->hasChanged(MegaChatCall::CHANGE_TYPE_SESSION_STATUS))
    {
       MegaChatHandle peerid = call->getPeerSessionStatusChange();
       MegaChatHandle clientid = call->getClientidSessionStatusChange();
       MegaChatSession *session = call->getMegaChatSession(peerid, clientid);
       assert(session);
       switch (session->getStatus())
       {
           case MegaChatSession::SESSION_STATUS_IN_PROGRESS:
           {
               window->createCallGui(call->hasVideoInitialCall(), peerid, clientid);
               window->connectPeerCallGui(peerid, clientid);

               break;
           }

           case MegaChatSession::SESSION_STATUS_DESTROYED:
               window->destroyCallGui(peerid, clientid);
               break;
       }
    }
}
#endif

ChatWindow *MainWindow::getChatWindowIfExists(MegaChatHandle chatId)
{
    ChatWindow *window = nullptr;
    ChatListItemController *itemController = getChatControllerById(chatId);
    if (itemController)
    {
        window = itemController->getChatWindow();
    }
    return window;
}

void MainWindow::clearQtContactWidgetList()
{
    ui->contactList->clear();
}

void MainWindow::clearQtChatWidgetList()
{
    ui->chatList->clear();
}

void MainWindow::clearContactWidgets()
{
    std::map<megachat::MegaChatHandle, ContactListItemController *>::iterator it;
    for (it = mContactControllers.begin(); it != mContactControllers.end(); it++)
    {
        ContactListItemController *itemController = it->second;
        if(itemController)
        {
            itemController->addOrUpdateWidget(nullptr);
        }
    }
}

void MainWindow::clearChatWidgets()
{
    std::map<megachat::MegaChatHandle, ChatListItemController *>::iterator it;
    for (it = mChatControllers.begin(); it != mChatControllers.end(); it++)
    {
        ChatListItemController *itemController = it->second;
        if(itemController)
        {
            itemController->addOrUpdateWidget(nullptr);
        }
    }
}

void MainWindow::clearChatControllers()
{
    std::map<megachat::MegaChatHandle, ChatListItemController *>::iterator it;
    for (it = mChatControllers.begin(); it != mChatControllers.end(); it++)
    {
        ChatListItemController *itemController = it->second;
        delete itemController;
    }
    mChatControllers.clear();
}

void MainWindow::addOrUpdateContactControllersItems(MegaUserList *contactList)
{
    MegaUser *contact = NULL;

    for (int i = 0; i < contactList->size(); i++)
    {
        contact = contactList->get(i);
        ::mega::MegaHandle userHandle = contact->getHandle();
        if (userHandle != mMegaChatApi->getMyUserHandle())
        {
            ContactListItemController *itemController = getContactControllerById(contact->getHandle());
            if (!itemController)
            {
                itemController = new ContactListItemController(contact->copy());
                mContactControllers.insert(std::pair<megachat::MegaChatHandle, ContactListItemController *>(contact->getHandle(), itemController));
            }
            else
            {
                itemController->addOrUpdateItem(contact->copy());
            }
        }
    }
}

void MainWindow::reorderAppContactList()
{
    //Clean contacts Qt widgets container
    clearQtContactWidgetList();

    // Clean the ContacItemWidgets in ContactListItemController list
    clearContactWidgets();

    addQtContactWidgets();
}

void MainWindow::reorderAppChatList()
{
    mNeedReorder = false;

    //Clean chats Qt widgets container
    clearQtChatWidgetList();

    //Clean the ChatItemWidgets in ChatListItemController list
    clearChatWidgets();

    //Add archived chats
    if (mShowArchived)
    {
        addChatsBystatus(chatArchivedStatus);
    }

    //Add active/inactive chats
    addChatsBystatus(chatNotArchivedStatus);

    //Prepare tag to indicate chatrooms shown
    QString text;
    if (mShowArchived)
    {
        text.append(" Showing <all> chatrooms");
    }
    else
    {
        text.append(" Showing <active+inactive> chatrooms");
    }
    ui->mOnlineStatusDisplay->setText(text);
}

void MainWindow::addQtContactWidgets()
{
    ui->mContacsSeparator->setText(" Loading contacts");
    setNContacts(mContactControllers.size());

    std::map<megachat::MegaChatHandle, ContactListItemController *>::iterator it;
    for (it = mContactControllers.begin(); it != mContactControllers.end(); it++)
    {
        MegaUser *contact = NULL;
        ContactListItemController *itemController = it->second;
        assert(itemController);
        contact = itemController->getItem();
        mega::MegaHandle userHandle = contact->getHandle();
        if (userHandle != mMegaChatApi->getMyUserHandle())
        {
            //Add Qt widget
            ContactItemWidget *widget = addQtContactWidget(contact);

            //Add or update widget in ContactListItemController
            itemController->addOrUpdateWidget(widget);
        }
    }

    if (mContactControllers.size() > 0)
    {
        ui->mContacsSeparator->setText("Showing <active> contacts");
    }
}

void MainWindow::addChatsBystatus(const int status)
{
    std::list<Chat> *chatList = getLocalChatListItemsByStatus(status);
    chatList->sort();
    for (Chat &chat : (*chatList))
    {
        const megachat::MegaChatListItem *auxItem = chat.chatItem;
        ChatListItemController *itemController = this->getChatControllerById(auxItem->getChatId());
        assert(itemController);

        //Add Qt widget
        ChatItemWidget *widget = addQtChatWidget(itemController->getItem());

        //Add or update widget in ChatListItemController
        itemController->addOrUpdateWidget(widget);
    }
    delete chatList;
}

bool MainWindow::eventFilter(QObject *, QEvent *event)
{
    if (this->mMegaChatApi->isSignalActivityRequired() && event->type() == QEvent::MouseButtonRelease)
    {
        this->mMegaChatApi->signalPresenceActivity();
    }
    return false;
}


void MainWindow::on_bSettings_clicked()
{
    QMenu menu(this);

    menu.setAttribute(Qt::WA_DeleteOnClose);


    // Chats
    QMenu *chatMenu = menu.addMenu("Chats");
    auto actPeerChat = chatMenu->addAction(tr("Create 1on1 chat"));
    connect(actPeerChat, &QAction::triggered, this, [=](){onAddChatRoom(false,false);});

    auto actGroupChat = chatMenu->addAction(tr("Create group chat"));
    connect(actGroupChat, &QAction::triggered, this, [=](){onAddChatRoom(true, false);});

    auto actPubChat = chatMenu->addAction(tr("Create public chat"));
    connect(actPubChat, &QAction::triggered, this, [=](){onAddChatRoom(true, true);});

    auto actPreviewChat = chatMenu->addAction(tr("Open chat preview"));
    connect(actPreviewChat,  &QAction::triggered, this, [this] {openChatPreview(true);});

    auto actCheckLink = chatMenu->addAction(tr("Check chat-link"));
    connect(actCheckLink,  &QAction::triggered, this, [this] {openChatPreview(false);});

    auto actArchived = chatMenu->addAction(tr("Show archived chats"));
    connect(actArchived, SIGNAL(triggered()), this, SLOT(onShowArchivedChats()));
    actArchived->setCheckable(true);
    actArchived->setChecked(mShowArchived);

    // Contacts
    QMenu *contactsMenu = menu.addMenu("Contacts");
    auto addAction = contactsMenu->addAction(tr("Add user to contacts"));
    connect(addAction, SIGNAL(triggered()), this, SLOT(onAddContact()));

    // Settings
    QMenu *settingsMenu = menu.addMenu("Settings");
    auto actWebRTC = settingsMenu->addAction(tr("Set audio/video input devices"));
    connect(actWebRTC, SIGNAL(triggered()), this, SLOT(onWebRTCsetting()));

    auto actTwoFactCheck = settingsMenu->addAction(tr("Enable/Disable 2FA"));
    connect(actTwoFactCheck, &QAction::triggered, this, [=](){onTwoFactorCheck();});
    actTwoFactCheck->setEnabled(mMegaApi->multiFactorAuthAvailable());

    MegaChatPresenceConfig *presenceConfig = mMegaChatApi->getPresenceConfig();
    auto actlastGreenVisible = settingsMenu->addAction("Enable/Disable Last-Green");
    connect(actlastGreenVisible, SIGNAL(triggered()), this, SLOT(onlastGreenVisibleClicked()));

    if (presenceConfig)
    {
        actlastGreenVisible->setCheckable(true);
        actlastGreenVisible->setChecked(presenceConfig->isLastGreenVisible());
    }
    else
    {
        actlastGreenVisible->setEnabled(false);
    }
    delete presenceConfig;


    // Other options
    QMenu *othersMenu = menu.addMenu("Others");

    auto actPrintMyInfo = othersMenu->addAction(tr("Print my info"));
    connect(actPrintMyInfo, SIGNAL(triggered()), this, SLOT(onPrintMyInfo()));

    auto actForceReconnect = othersMenu->addAction(tr("Force reconnect"));
    connect(actForceReconnect,  &QAction::triggered, this, [this] {onReconnect(true);});

    auto actRetryPendingConn = othersMenu->addAction(tr("Retry pending connections"));
    connect(actRetryPendingConn,  &QAction::triggered, this, [this] {onReconnect(false);});

    auto actPushAndReceived = othersMenu->addAction(tr("Push received (Android)"));
    connect(actPushAndReceived,  &QAction::triggered, this, [this] {onPushReceived(0);});

    auto actPushReceived = othersMenu->addAction(tr("Push received (iOS)"));
    connect(actPushReceived,  &QAction::triggered, this, [this] {onPushReceived(1);});

    auto actCatchUp = othersMenu->addAction(tr("Catch-Up with API"));
    connect(actCatchUp, SIGNAL(triggered()), this, SLOT(onCatchUp()));

    auto actUseStaging = othersMenu->addAction("Use API staging");
    connect(actUseStaging, SIGNAL(toggled(bool)), this, SLOT(onUseApiStagingClicked(bool)));
    actUseStaging->setCheckable(true);
    actUseStaging->setChecked(mApp->isStagingEnabled());

    QPoint pos = ui->bSettings->pos();
    pos.setX(pos.x() + ui->bSettings->width());
    pos.setY(pos.y() + ui->bSettings->height());
    menu.exec(mapToGlobal(pos));
}

void MainWindow::onReconnect(bool disconnect)
{
    mMegaChatApi->retryPendingConnections(disconnect);
    mMegaApi->retryPendingConnections(disconnect);
}

void MainWindow::onPushReceived(unsigned int type)
{
    if (!type)
    {
        mMegaChatApi->pushReceived(false);
    }
    else
    {
        std::string aux = mApp->getText();
        if (!aux.size())
            return;

        MegaChatHandle chatid = (aux.size() > 1)
                ? mMegaApi->base64ToUserHandle(aux.c_str())
                : MEGACHAT_INVALID_HANDLE;

        mMegaChatApi->pushReceived(false, chatid);
    }
}

void MainWindow::openChatPreview(bool create)
{
    std::string chatLink;
    QString qChatLink = QInputDialog::getText(this, tr("Load chat link"), tr("Enter a valid chatlink"));
    if (!qChatLink.isNull())
    {
        chatLink = qChatLink.toStdString();
        if (chatLink.empty())
        {
            QMessageBox::warning(this, tr("Load chat link"), tr("You can't enter an empty chatlink"));
            return;
        }
       create
        ?mMegaChatApi->openChatPreview(chatLink.c_str())
        :mMegaChatApi->checkChatLink(chatLink.c_str());
    }
}


void MainWindow::onPrintMyInfo()
{
    QMessageBox msg;
    msg.setIcon(QMessageBox::Information);
    msg.setText(this->ui->bOnlineStatus->toolTip());
    msg.exec();
}

void MainWindow::updateToolTipMyInfo()
{
    QString text = NULL;
    megachat::MegaChatHandle myHandle = mMegaChatApi->getMyUserHandle();
    char *myMail = mMegaChatApi->getMyEmail();
    std::string myHandleBin = std::to_string(myHandle);
    const char *myHandle_64 = mMegaApi->userHandleToBase64(myHandle);
    text.append("\nMy email: ");
    text.append(myMail);
    text.append("\nMy User handle Bin: ");
    text.append(myHandleBin.c_str());
    text.append("\nMy User handle B64: ");
    text.append(QString::fromStdString(myHandle_64));
    ui->bOnlineStatus->setToolTip(text);
    delete [] myHandle_64;
    delete [] myMail;
}

void MainWindow::onWebRTCsetting()
{
    #ifndef KARERE_DISABLE_WEBRTC
        this->mMegaChatApi->loadAudioVideoDeviceList();
    #endif
}

void MainWindow::createSettingsMenu()
{
    ChatSettingsDialog *chatSettings = new ChatSettingsDialog(this, mChatSettings);
    chatSettings->exec();
    chatSettings->deleteLater();
}

void MainWindow::on_bOnlineStatus_clicked()
{
    onlineStatus = new QMenu(this);
    auto actOnline = onlineStatus->addAction("Online");
    actOnline->setData(QVariant(MegaChatApi::STATUS_ONLINE));
    connect(actOnline, SIGNAL(triggered()), this, SLOT(setOnlineStatus()));

    auto actAway = onlineStatus->addAction("Away");
    actAway->setData(QVariant(MegaChatApi::STATUS_AWAY));
    connect(actAway, SIGNAL(triggered()), this, SLOT(setOnlineStatus()));

    auto actDnd = onlineStatus->addAction("Busy");
    actDnd->setData(QVariant(MegaChatApi::STATUS_BUSY));
    connect(actDnd, SIGNAL(triggered()), this, SLOT(setOnlineStatus()));

    auto actOffline = onlineStatus->addAction("Offline");
    actOffline->setData(QVariant(MegaChatApi::STATUS_OFFLINE));
    connect(actOffline, SIGNAL(triggered()), this, SLOT(setOnlineStatus()));

    QPoint pos = ui->bOnlineStatus->pos();
    pos.setX(pos.x() + ui->bOnlineStatus->width());
    pos.setY(pos.y() + ui->bOnlineStatus->height());

    onlineStatus->setStyleSheet("QMenu {"
        "background-color: qlineargradient("
        "spread:pad, x1:0, y1:0, x2:0, y2:1,"
            "stop:0 rgba(120,120,120,200),"
            "stop:1 rgba(180,180,180,200));"
        "}"
        "QMenu::item:!selected{"
            "color: white;"
        "}"
        "QMenu::item:selected{"
            "background-color: qlineargradient("
            "spread:pad, x1:0, y1:0, x2:0, y2:1,"
            "stop:0 rgba(120,120,120,200),"
            "stop:1 rgba(180,180,180,200));"
        "}");
    onlineStatus->exec(mapToGlobal(pos));
    onlineStatus->deleteLater();
}

void MainWindow::onShowArchivedChats()
{
    mShowArchived = !mShowArchived;
    reorderAppChatList();
}

ContactItemWidget *MainWindow::addQtContactWidget(MegaUser *user)
{
    //Create widget and add to interface
    int index = -(mArchivedChats + mNContacts);
    mNContacts += 1;
    ContactItemWidget *widget = new ContactItemWidget(ui->contactList, this, mMegaChatApi, mMegaApi, user);
    widget->updateToolTip(user);
    QListWidgetItem *item = new QListWidgetItem();
    widget->setWidgetItem(item);
    item->setSizeHint(QSize(item->sizeHint().height(), 28));
    ui->contactList->insertItem(index, item);
    ui->contactList->setItemWidget(item, widget);
    return widget;
}

ContactListItemController *MainWindow::addOrUpdateContactController(MegaUser *user)
{
    //If no controller exists we need to create
    std::map<mega::MegaHandle, ContactListItemController *>::iterator itContacts;
    itContacts = mContactControllers.find(user->getHandle());
    ContactListItemController *itemController;
    if (itContacts == mContactControllers.end())
    {
         itemController = new ContactListItemController(user);
    }
    else
    {
         //If controller exists we need to update item
         itemController = (ContactListItemController *) itContacts->second;
         itemController->addOrUpdateItem(user);
    }

    return itemController;
}

ChatListItemController *MainWindow::addOrUpdateChatControllerItem(MegaChatListItem *chatListItem)
{
    //If no controller exists we need to create
    std::map<mega::MegaHandle, ChatListItemController *>::iterator it;
    it = mChatControllers.find(chatListItem->getChatId());
    ChatListItemController *itemController;
    if (it == mChatControllers.end())
    {
         itemController = new ChatListItemController(chatListItem);
         mChatControllers.insert(std::pair<megachat::MegaChatHandle, ChatListItemController *>(chatListItem->getChatId(), itemController));
    }
    else
    {
         //If controller exists we need to update item
         itemController = (ChatListItemController *) it->second;
         itemController->addOrUpdateItem(chatListItem);
    }
    return itemController;
}

void MainWindow::closeChatPreview(megachat::MegaChatHandle chatId)
{
    ChatListItemController *itemController = nullptr;
    std::map<mega::MegaHandle, ChatListItemController *> ::iterator it;
    it = this->mChatControllers.find(chatId);
    if (it != mChatControllers.end())
    {
        itemController = it->second;
        megachat::MegaChatListItem *item = itemController->getItem();
        assert(item);
        if (!item->isPreview())
        {
            return;
        }

        mMegaChatApi->closeChatPreview(chatId);
        ChatWindow * auxWindow = itemController->getChatWindow();
        if(auxWindow)
        {
            auxWindow->deleteLater();
        }

        delete itemController;
        mChatControllers.erase(it);
        reorderAppChatList();
    }
}

ChatItemWidget *MainWindow::addQtChatWidget(const MegaChatListItem *chatListItem)
{
    //Create widget and add to interface
    int index = 0;
    if (chatListItem->isArchived())
    {
        index = -(mArchivedChats);
        mArchivedChats += 1;
    }
    else if (!chatListItem->isActive())
    {
        index = -(mNContacts + mArchivedChats + mInactiveChats);
        mInactiveChats += 1;
    }
    else
    {
        index = -(mActiveChats + mInactiveChats + mArchivedChats+mNContacts);
        mActiveChats += 1;
    }

    megachat::MegaChatHandle chathandle = chatListItem->getChatId();
    ChatItemWidget *widget = new ChatItemWidget(this, mMegaChatApi, chatListItem);
    widget->updateToolTip(chatListItem, NULL);
    QListWidgetItem *item = new QListWidgetItem();
    widget->setWidgetItem(item);
    item->setSizeHint(QSize(item->sizeHint().height(), 28));
    ui->chatList->insertItem(index, item);
    ui->chatList->setItemWidget(item, widget);
    return widget;
}

void MainWindow::onChatListItemUpdate(MegaChatApi *, MegaChatListItem *item)
{
    int oldPriv;
    ChatItemWidget *widget = nullptr;
    ChatListItemController *itemController = getChatControllerById(item->getChatId());

    //Get a copy of old privilege
    if (itemController)
    {
        if (itemController->getItem())
        {
           oldPriv = itemController->getItem()->getOwnPrivilege();
        }

        widget = itemController->getWidget();
    }
    itemController = addOrUpdateChatControllerItem(item->copy());

    if (!mAllowOrder
        && !(item->hasChanged(megachat::MegaChatListItem::CHANGE_TYPE_UPDATE_PREVIEWERS)))
    {
        return;
    }

    // If we don't need to reorder and chatItemwidget is rendered
    // we need to update the widget because non order actions requires
    // a live update of widget
    if (!needReorder(item, oldPriv) && widget)
    {
        //Last Message update
        if (item->hasChanged(megachat::MegaChatListItem::CHANGE_TYPE_LAST_MSG))
        {
            widget->updateToolTip(item, NULL);
        }

        //Unread count update
        if (item->hasChanged(megachat::MegaChatListItem::CHANGE_TYPE_UNREAD_COUNT))
        {
            widget->onUnreadCountChanged(item->getUnreadCount());
        }

        //Title update
        if (item->hasChanged(megachat::MegaChatListItem::CHANGE_TYPE_TITLE))
        {
            widget->onTitleChanged(item->getTitle());
        }

        //Own priv update
        if (item->hasChanged(megachat::MegaChatListItem::CHANGE_TYPE_OWN_PRIV))
        {
            widget->updateToolTip(item, NULL);
        }

        //Participants update
        if (item->hasChanged(megachat::MegaChatListItem::CHANGE_TYPE_PARTICIPANTS))
        {
            widget->updateToolTip(item, NULL);
        }

        if (item->hasChanged(megachat::MegaChatRoom::CHANGE_TYPE_UPDATE_PREVIEWERS))
        {
            widget->onPreviewersCountChanged(item->getNumPreviewers());
            ChatWindow *win = itemController->getChatWindow();
            if (win)
            {
                win->updatePreviewers(item->getNumPreviewers());
            }
        }
    }
    else if(mNeedReorder)
    {
        reorderAppChatList();
    }
}

bool MainWindow::needReorder(MegaChatListItem *newItem, int oldPriv)
{
    if(newItem->hasChanged(megachat::MegaChatListItem::CHANGE_TYPE_CLOSED)
         || newItem->hasChanged(megachat::MegaChatListItem::CHANGE_TYPE_LAST_TS)
         || newItem->hasChanged(megachat::MegaChatListItem::CHANGE_TYPE_ARCHIVE)
         || newItem->hasChanged(megachat::MegaChatListItem::CHANGE_TYPE_UNREAD_COUNT)
         || newItem->hasChanged(megachat::MegaChatRoom::CHANGE_TYPE_CHAT_MODE)
         || (newItem->getOwnPrivilege() == megachat::MegaChatRoom::PRIV_RM)         
         || ((oldPriv == megachat::MegaChatRoom::PRIV_RM)
             &&(newItem->getOwnPrivilege() > megachat::MegaChatRoom::PRIV_RM)))
    {
        mNeedReorder = true;
    }

    return mNeedReorder;
}

void MainWindow::activeControls(bool active)
{
    if (active)
    {
        ui->bOnlineStatus->show();
        ui->mLogout->show();
    }
    else
    {        
        ui->bOnlineStatus->hide();
    }
}

void MainWindow::onAddChatRoom(bool isGroup, bool isPublic)
{
    ::mega::MegaUserList *list = mMegaApi->getContacts();
    ChatGroupDialog *chatDialog = new ChatGroupDialog(this, isGroup, isPublic, mMegaChatApi);
    chatDialog->createChatList(list);
    chatDialog->show();
}

char *MainWindow::askChatTitle()
{
    char *title = NULL;
    std::string auxTitle = QInputDialog::getText(this, tr("Set chat title"), tr("Leave blank for default title")).toStdString();
    if (!auxTitle.empty())
    {
        title = new char[auxTitle.size() + 1];
        strcpy(title, auxTitle.c_str());
    }
    return title;
}

void MainWindow::onAddContact()
{
    QString email = QInputDialog::getText(this, tr("Add contact"), tr("Please enter the email of the user to add"));
    if (email.isNull())
        return;

    char *myEmail = mMegaApi->getMyEmail();
    QString qMyEmail = myEmail;
    delete [] myEmail;

    if (email == qMyEmail)
    {
        QMessageBox::critical(this, tr("Add contact"), tr("You can't add your own email as contact"));
        return;
    }
    std::string emailStd = email.toStdString();
    mMegaApi->inviteContact(emailStd.c_str(),tr("I'd like to add you to my contact list").toUtf8().data(), MegaContactRequest::INVITE_ACTION_ADD);
}

void MainWindow::setOnlineStatus()
{
    auto action = qobject_cast<QAction*>(QObject::sender());
    assert(action);
    bool ok;
    auto pres = action->data().toUInt(&ok);
    if (!ok || (pres == MegaChatApi::STATUS_INVALID))
    {
        return;
    }
    this->mMegaChatApi->setOnlineStatus(pres);
}

void MainWindow::onChatConnectionStateUpdate(MegaChatApi *, MegaChatHandle chatid, int newState)
{
    if (chatid == megachat::MEGACHAT_INVALID_HANDLE)
    {
        // When we are connected to all chats we have to reorder the chatlist
        // we skip all reorders until we receive this event to avoid app overload
        mAllowOrder = true;

        //Reorder chat list in QtApp
        reorderAppChatList();
        return;
    }

    ChatListItemController *itemController = getChatControllerById(chatid);
    if (itemController)
    {
       ChatItemWidget *widget = itemController->getWidget();
       if (widget)
       {
            widget->onlineIndicatorUpdate(newState);
       }
    }
}

void MainWindow::onChatInitStateUpdate(megachat::MegaChatApi *, int newState)
{
    if (newState == MegaChatApi::INIT_ERROR)
    {
        QMessageBox msgBox;
        msgBox.setText("Critical error in MEGAchat. The application will close now. If the problem persists, you can delete your cached sessions.");
        msgBox.setStandardButtons(QMessageBox::Ok);
        int ret = msgBox.exec();

        if (ret == QMessageBox::Ok)
        {
            deleteLater();
            return;
        }
    }

    if (newState == MegaChatApi::INIT_ONLINE_SESSION || newState == MegaChatApi::INIT_OFFLINE_SESSION)
    {
        if(!isVisible())
        {
            mApp->resetLoginDialog();
            show();
        }

        const char *myEmail = mMegaChatApi->getMyEmail();
        QString auxTitle(myEmail);
        delete [] myEmail;

        if (mApp->sid() && newState == MegaChatApi::INIT_OFFLINE_SESSION)
        {
            auxTitle.append(" [OFFLINE MODE]");
        }

        if (auxTitle.size())
        {
            setWindowTitle(auxTitle);
        }

        //Update chatListItems in chatControllers
        updateChatControllersItems();

        //Reorder chat list in QtApp
        reorderAppChatList();

        //Update my user info tooltip
        updateToolTipMyInfo();
    }
}

void MainWindow::onChatOnlineStatusUpdate(MegaChatApi *, MegaChatHandle userhandle, int status, bool inProgress)
{
    if (status == megachat::MegaChatApi::STATUS_INVALID)
    {
        // If we don't receive our presence we'll skip all chats reorders
        // when we are connected to all chats this flag will be set true
        // and chatlist will be reordered
        mAllowOrder = false;
        status = 0;
    }

    if (mMegaChatApi->getMyUserHandle() == userhandle && !inProgress)
    {
        ui->bOnlineStatus->setText(kOnlineSymbol_Set);
        if (status >= 0 && status < NINDCOLORS)
            ui->bOnlineStatus->setStyleSheet(kOnlineStatusBtnStyle.arg(gOnlineIndColors[status]));
    }
    else
    {
        std::map<mega::MegaHandle, ContactListItemController *>::iterator itContacts;
        itContacts = this->mContactControllers.find((mega::MegaHandle) userhandle);
        if (itContacts != mContactControllers.end())
        {
            ContactListItemController *itemController = itContacts->second;
            assert(!inProgress);

            ContactItemWidget *widget = itemController->getWidget();
            if (widget)
            {
                widget->updateOnlineIndicator(status);
            }
        }
    }
}

void MainWindow::onChatPresenceConfigUpdate(MegaChatApi *, MegaChatPresenceConfig *config)
{
    int status = config->getOnlineStatus();
    if (status == megachat::MegaChatApi::STATUS_INVALID)
        status = 0;

    ui->bOnlineStatus->setText(config->isPending()
        ? kOnlineSymbol_InProgress
        : kOnlineSymbol_Set);

    ui->bOnlineStatus->setStyleSheet(
                kOnlineStatusBtnStyle.arg(gOnlineIndColors[status]));
}

void MainWindow::onChatPresenceLastGreen(MegaChatApi */*api*/, MegaChatHandle userhandle, int lastGreen)
{
    const char *firstname = mApp->getFirstname(userhandle);
    if (!firstname)
    {
        firstname = mMegaApi->userHandleToBase64(userhandle);
    }

    std::string str;
    str.append("User: ");
    str.append(firstname);
    str.append("\nLast time green: ");
    str.append(std::to_string(lastGreen));
    str.append(" minutes ago");

    QMessageBox *msgBox = new QMessageBox(this);
    msgBox->setIcon( QMessageBox::Information );
    msgBox->setAttribute(Qt::WA_DeleteOnClose);
    msgBox->setStandardButtons(QMessageBox::Ok);
    msgBox->setWindowTitle( tr("Last time green"));
    msgBox->setText(str.c_str());
    msgBox->setModal(false);
    msgBox->show();
    delete [] firstname;
}

void MainWindow::setNContacts(int nContacts)
{
    this->mNContacts = nContacts;
}

void MainWindow::updateMessageFirstname(MegaChatHandle contactHandle, const char *firstname)
{
    std::map<megachat::MegaChatHandle, ChatListItemController *>::iterator it;
    for (it = mChatControllers.begin(); it != mChatControllers.end(); it++)
    {
        ChatListItemController *itemController = it->second;
        const MegaChatListItem *item = itemController->getItem();
        ChatItemWidget *widget = itemController->getWidget();

        if (item && widget && item->getLastMessageSender() == contactHandle)
        {
            widget->updateToolTip(item, firstname);
        }

        ChatWindow *chatWindow = itemController->getChatWindow();
        if (chatWindow)
        {
            chatWindow->updateMessageFirstname(contactHandle, firstname);
        }
    }
}

void MainWindow::updateChatControllersItems()
{
    //Clean chatController list
    clearChatControllers();

    //Add all active chat controllers
    MegaChatListItemList *chatList = mMegaChatApi->getActiveChatListItems();
    for (unsigned int i = 0; i < chatList->size(); i++)
    {
        addOrUpdateChatControllerItem(chatList->get(i)->copy());
    }
    delete chatList;

    //Add inactive chat controllers
    chatList = mMegaChatApi->getInactiveChatListItems();
    for (unsigned int i = 0; i < chatList->size(); i++)
    {
        addOrUpdateChatControllerItem(chatList->get(i)->copy());
    }
    delete chatList;

    //Add archived chat controllers
    chatList = mMegaChatApi->getArchivedChatListItems();
    for (unsigned int i = 0; i < chatList->size(); i++)
    {
        addOrUpdateChatControllerItem(chatList->get(i)->copy());
    }
    delete chatList;
}

ContactListItemController *MainWindow::getContactControllerById(MegaChatHandle userId)
{
    std::map<mega::MegaHandle, ContactListItemController *> ::iterator it;
    it = this->mContactControllers.find(userId);
    if (it != mContactControllers.end())
    {
        return it->second;
    }

    return nullptr;
}

ChatListItemController *MainWindow::getChatControllerById(MegaChatHandle chatId)
{
    std::map<mega::MegaHandle, ChatListItemController *> ::iterator it;
    it = this->mChatControllers.find(chatId);
    if (it != mChatControllers.end())
    {
        return it->second;
    }

    return nullptr;
}

std::list<Chat> *MainWindow::getLocalChatListItemsByStatus(int status)
{
    std::list<Chat> *chatList = new std::list<Chat>;
    std::map<megachat::MegaChatHandle, ChatListItemController *>::iterator it;

    for (it = mChatControllers.begin(); it != mChatControllers.end(); it++)
    {
        ChatListItemController *itemController = it->second;
        const megachat::MegaChatListItem *item = itemController->getItem();

        assert(item);
        switch (status)
        {
            case chatNotArchivedStatus:
                if (!item->isArchived())
                {
                    chatList->push_back(Chat(item));
                }
                break;

            case chatArchivedStatus:
                if (item->isArchived())
                {
                    chatList->push_back(Chat(item));
                }
                break;
        }
    }
    return chatList;
}


void MainWindow::updateContactFirstname(MegaChatHandle contactHandle, const char *firstname)
{
    std::map<mega::MegaHandle, ContactListItemController *>::iterator itContacts;
    itContacts = mContactControllers.find(contactHandle);

    if (itContacts != mContactControllers.end())
    {
        ContactListItemController *itemController = itContacts->second;
        itemController->getWidget()->updateTitle(firstname);
    }
}

void MainWindow::on_mLogout_clicked()
{
    QMessageBox msgBox;
    msgBox.setText("Do you want to logout?");
    msgBox.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
    msgBox.setDefaultButton(QMessageBox::Save);
    int ret = msgBox.exec();
    if (ret == QMessageBox::Ok)
    {
        removeListeners();
        clearChatControllers();
        clearContactControllersMap();
        if (mMegaChatApi->getInitState() == MegaChatApi::INIT_ANONYMOUS)
        {
            emit onAnonymousLogout();
        }
        else
        {
            mMegaApi->logout();
        }
    }    
}

void MainWindow::onCatchUp()
{
    mMegaApi->catchup();
}

void MainWindow::onlastGreenVisibleClicked()
{
    MegaChatPresenceConfig *presenceConfig = mMegaChatApi->getPresenceConfig();
    mMegaChatApi->setLastGreenVisible(!presenceConfig->isLastGreenVisible());
    delete presenceConfig;
}

void MainWindow::onUseApiStagingClicked(bool enable)
{
    mApp->enableStaging(enable);
}
