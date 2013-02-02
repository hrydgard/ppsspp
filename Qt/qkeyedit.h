#ifndef QKEYEDIT_H
#define QKEYEDIT_H

#include <QLineEdit>

class QKeyEdit : public QLineEdit
{
	Q_OBJECT
public:
	explicit QKeyEdit(QWidget *parent = 0);

protected:
	bool event(QEvent *e);
signals:
	
public slots:
	
};

#endif // QKEYEDIT_H
