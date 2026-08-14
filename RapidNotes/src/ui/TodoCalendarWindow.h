#ifndef TODOCALENDARWINDOW_H
#define TODOCALENDARWINDOW_H

#include "FramelessDialog.h"
#include "../core/DatabaseManager.h"
#include <QCalendarWidget>
#include <QListWidget>
#include <QStackedWidget>
#include <QLineEdit>
#include <QTextEdit>
#include <QComboBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QSlider>
#include <QPushButton>
#include <QLabel>
#include <QDateTime>
#include <QDate>

class CustomCalendar : public QCalendarWidget {
    Q_OBJECT
public:
    explicit CustomCalendar(QWidget* parent = nullptr);
protected:
    void paintCell(QPainter* painter, const QRect& rect, QDate date) const override;
};

class TodoCalendarWindow : public FramelessDialog {
    Q_OBJECT
public:
    explicit TodoCalendarWindow(QWidget* parent = nullptr);
    ~TodoCalendarWindow() = default;

public slots:
    void onAddAlarm();
    void onAddTodo();

protected:
    void showEvent(QShowEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void onDateSelected();
    void onSwitchView();
    void onGotoToday();
    void onEditTodo(QListWidgetItem* item);
    void onDetailedItemDoubleClicked(QListWidgetItem* item);
    void refreshTodos();

private:
    void initUI();
    void update24hList(const QDate& date);
    void openEditDialog(const DatabaseManager::Todo& t);
    
    CustomCalendar* m_calendar;
    QStackedWidget* m_viewStack;
    QListWidget* m_detailed24hList;
    QListWidget* m_todoList;
    QPushButton* m_btnAdd;
    QPushButton* m_btnSwitch;
    QPushButton* m_btnToday;
    QPushButton* m_btnAlarm;
    QLabel* m_dateLabel;

    QLineEdit* m_searchEdit;
    QWidget* m_tabBar;
    QList<QPushButton*> m_tabButtons;
    int m_currentTabIdx = 0; // 0:全部, 1:待办, 2:进行中, 3:已完成
};

class CustomDateTimeEdit : public QWidget {
    Q_OBJECT
public:
    explicit CustomDateTimeEdit(const QDateTime& dt, QWidget* parent = nullptr);
    void setDateTime(const QDateTime& dt);
    QDateTime dateTime() const { return m_dateTime; }

signals:
    void dateTimeChanged(const QDateTime& dt);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void updateDisplay();
    void showPicker();
    QDateTime m_dateTime;
    QLineEdit* m_display;
    QPushButton* m_btn;
};

class TodoReminderDialog : public FramelessDialog {
    Q_OBJECT
public:
    explicit TodoReminderDialog(const DatabaseManager::Todo& todo, QWidget* parent = nullptr);

signals:
    void snoozeRequested(int minutes);

private:
    DatabaseManager::Todo m_todo;
};

class AlarmEditDialog : public FramelessDialog {
    Q_OBJECT
public:
    explicit AlarmEditDialog(const DatabaseManager::Todo& todo, QWidget* parent = nullptr);
    DatabaseManager::Todo getTodo() const;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void onSave();

private:
    void initUI();
    DatabaseManager::Todo m_todo;

    QLineEdit* m_editTitle;
    QSpinBox* m_hSpin;
    QSpinBox* m_mSpin;
    QComboBox* m_comboRepeat;
};

class TodoEditDialog : public FramelessDialog {
    Q_OBJECT
public:
    explicit TodoEditDialog(const DatabaseManager::Todo& todo, QWidget* parent = nullptr);
    DatabaseManager::Todo getTodo() const;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void onSave();

private:
    void initUI();
    DatabaseManager::Todo m_todo;
    
    QLineEdit* m_editTitle;
    QTextEdit* m_editContent;
    CustomDateTimeEdit* m_editStart;
    CustomDateTimeEdit* m_editEnd;
    CustomDateTimeEdit* m_editReminder;
    QComboBox* m_comboPriority;
    QComboBox* m_comboRepeat;
    QSlider* m_sliderProgress;
    QLabel* m_labelProgress;
    QCheckBox* m_checkReminder;
};

#endif // TODOCALENDARWINDOW_H
