#include "folderIconSelector.h"
#include "ui_folderIconSelector.h"
#include <QDir>
#include "utils.h"
#include <QDebug>
#include <QListView>
#include <QTimer>
#include <QContextMenuEvent>
#include <QMenu>
#include <shlwapi.h>
#include <QElapsedTimer>
#include <QDesktopServices>
#include <QtConcurrent>

FolderIconSelector::FolderIconSelector(const QString& dirPath, QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::FolderIconSelector)
    , dirPath(dirPath)
{
    ui->setupUi(this);
    // 不加这句的话，无法设置item高度（原因未知）
    // https://blog.csdn.net/lilongwei_123/article/details/109503886
    ui->comboBox->setView(new QListView());

    ui->comboBox->installEventFilter(this);

    connect(ui->btn_apply, &QPushButton::clicked, this, [=](){
        auto iconPath = ui->comboBox->currentData().toString();
        if (iconPath.isEmpty()) return;

        QDir dir(dirPath);
        if (dir.exists(iconPath)) // 若icon在folder内，则转为相对路径
            iconPath = QDir(dirPath).relativeFilePath(iconPath);

        qDebug() << "set folder icon:" << iconPath << dirPath;
        Util::setFolderIcon(dirPath, iconPath);

        setIcon(Util::getFileIcon(dirPath));
    });

    setIcon(Util::getFileIcon(dirPath));

    // 异步，否则QCombobox的宽度会不太正常（不对齐）
    QTimer::singleShot(0, this, [=](){
        QDir dir(dirPath);
        ui->label->setText(dir.isRoot() ? dirPath : dir.dirName());
        auto files = Util::getExeFiles(dirPath);
        // 展示可选exe图标
        ui->comboBox->setUpdatesEnabled(false);
        for (const QString& path : files) {
            QIcon icon = Util::getFileIcon(path);
            ui->comboBox->addItem(icon, QFileInfo(path).fileName(), QDir::toNativeSeparators(path));
        }
        // 选中当前文件夹的图标
        auto iconPath = Util::getFolderIconPath(dirPath);
        if (!iconPath.isEmpty()) {
            int index = ui->comboBox->findData(iconPath);
            if (index != -1)
                ui->comboBox->setCurrentIndex(index);
        }
        ui->comboBox->setUpdatesEnabled(true);

        ui->comboBox->setEnabled(false);
        ui->btn_apply->setEnabled(false);
        // 过滤没有自定义图标的exe
        QtConcurrent::run([=](){
            QList<int> idxs;
            for (int i = 0; i < ui->comboBox->count(); ++i) {
                auto filePath = ui->comboBox->itemData(i).toString();
                if (!Util::hasCustomIcon(filePath)) // 耗时操作，必须放在子线程
                    idxs << i;
            }
            emit removeItems(idxs); // GUI操作必须放在主线程，通过信号与槽传递
        });
    });

    qRegisterMetaType<QList<int>>("QList<int>"); // for signal
    connect(this, &FolderIconSelector::removeItems, this, [=](QList<int> idxs){
        for (int i = idxs.size() - 1; i >= 0; --i) {
            qDebug() << "clear default icon:" << ui->comboBox->itemText(idxs[i]);
            ui->comboBox->removeItem(idxs[i]);
        }
        ui->comboBox->setEnabled(true);
        ui->btn_apply->setEnabled(true);
    });
}

FolderIconSelector::~FolderIconSelector()
{
    delete ui;
}

void FolderIconSelector::setIcon(const QIcon& icon)
{
    auto iconSize = ui->icon->size();
    auto iconPixmap = icon.pixmap(iconSize); // size可能比iconSize大，很奇怪，必须缩放
    if (iconSize != iconPixmap.size())
        iconPixmap = iconPixmap.scaled(iconSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    ui->icon->setPixmap(iconPixmap);
}

void FolderIconSelector::openFolder()
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(dirPath));
}

bool FolderIconSelector::eventFilter(QObject* obj, QEvent* event)
{
    if(obj == ui->comboBox) {
        if (event->type() == QEvent::Wheel) // 拦截滚轮事件
            return true;
    }
    return QWidget::eventFilter(obj, event);
}

void FolderIconSelector::contextMenuEvent(QContextMenuEvent* event)
{
    QMenu menu(this);
    menu.addAction("Open Folder", this, [=]{
        openFolder();
    });
    menu.addAction("Restore Default Icon", this, [=]{  // del desktop.ini
        Util::restoreFolderIcon(dirPath);
        setIcon(Util::getFileIcon(dirPath));
    });
    menu.exec(event->globalPos());
}

void FolderIconSelector::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
        openFolder();
    QWidget::mouseDoubleClickEvent(event);
}
