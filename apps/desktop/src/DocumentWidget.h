#pragma once

#include <QWidget>

class QPdfDocument;
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
    [[nodiscard]] QString errorMessage() const;

private:
    void buildInterface();
    void showMetadata();
    void setCustomZoom(qreal factor);
    void jumpToPage(int oneBasedPage);

    QString filePath_;
    QPdfDocument* document_;
    QPdfView* view_;
    QPdfSearchModel* searchModel_;
    QSpinBox* pageSelector_;
    QString errorMessage_;
};
