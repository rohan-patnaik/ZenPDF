#pragma once

#include <QPdfDocument>
#include <QWidget>

class QPdfSearchModel;
class QPdfView;
class QSpinBox;

class DocumentWidget final : public QWidget {
    Q_OBJECT

public:
    explicit DocumentWidget(QString filePath, QWidget* parent = nullptr);

    [[nodiscard]] QString filePath() const;
    [[nodiscard]] QString displayName() const;
    [[nodiscard]] int pageCount() const;
    [[nodiscard]] bool isReady() const;
    [[nodiscard]] bool needsPassword() const;
    [[nodiscard]] QString errorMessage() const;
    [[nodiscard]] bool unlock(const QString& password);

private:
    void buildInterface();
    void showMetadata();
    void printDocument();
    void setCustomZoom(qreal factor);
    void jumpToPage(int oneBasedPage);
    void updateLoadState(QPdfDocument::Error error);

    QString filePath_;
    QPdfDocument* document_;
    QPdfView* view_;
    QPdfSearchModel* searchModel_;
    QSpinBox* pageSelector_;
    QString errorMessage_;
    bool interfaceBuilt_{false};
};
